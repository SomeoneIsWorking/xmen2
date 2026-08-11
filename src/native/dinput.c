/*
 * DirectInput 7, as much of it as this game asks for.
 *
 * One import -- DINPUT.dll!DirectInputCreateEx -- and everything else arrives
 * through COM vtables on the objects it hands back. libIGDisplay calls it twice,
 * from igWin32Window::userConstruct and igWin32ControllerManager::initialize,
 * both with dwVersion 0x700, and both immediately dispatch vtable slot 4
 * (EnumDevices) with an enumeration callback of their own.
 *
 * That callback is guest code, so enumerating a device means calling BACK into
 * the guest with a DIDEVICEINSTANCE the guest will read. Nothing here can be
 * faked cheaply: report a device and the game will create it, set a data format
 * on it, acquire it and poll it every frame.
 *
 * WHAT IT SERVES: the system keyboard and the system mouse, enumerated and
 * created for real, sharing src/native/dinput_device.c with the DirectInput 8
 * stack so the two cannot recognise different sets of devices.
 *
 * WHAT IT DOES NOT, said plainly because a silent zero here is
 * indistinguishable from working input: JOYSTICKS. An enumeration for that
 * class finds nothing and says so, so igWin32ControllerManager builds no
 * controllers. src/display/ has the SDL3 controller backend this should be fed
 * from; see issue #32.
 *
 * Every method that is reached but unimplemented aborts by NAME rather than
 * returning a plausible HRESULT, because DirectInput callers routinely ignore
 * return codes and read the out-parameter instead.
 */
#include "x86rt.h"
#include "x86rt_native.h"
#include "guest_heap.h"
#include "dinput_device.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define A(i) RD32(C->esp + 4u + (uint32_t)(i) * 4u)
#define THIS A(0)

/* COM is __stdcall: the callee pops `this` plus nargs arguments. */
static void ret_com(CPU *C, uint32_t hr, int nargs)
{
    C->eax = hr;
    C->esp += 4u + (uint32_t)(nargs + 1) * 4u;
}

#define S_OK          0x00000000u
#define S_FALSE       0x00000001u
#define E_NOINTERFACE 0x80004002u
#define DIERR_INVALIDPARAM 0x80070057u
#define DIERR_DEVICENOTREG 0x80040154u
#define DIERR_OUTOFMEMORY  0x8007000Eu

/* IDirectInput7 vtable, in order. IUnknown first, then IDirectInput, then the
   2/7 extensions -- this is the layout the shipped dinput.dll has, and the
   game's own dispatch at [vtable+0x10] for EnumDevices confirms slot 4. */
enum {
    VT_QueryInterface, VT_AddRef, VT_Release,
    VT_CreateDevice, VT_EnumDevices, VT_GetDeviceStatus,
    VT_RunControlPanel, VT_Initialize,
    VT_FindDevice,            /* IDirectInput2 */
    VT_CreateDeviceEx,        /* IDirectInput7 */
    VT_COUNT
};

static const char *const VT_NAME[VT_COUNT] = {
    "QueryInterface", "AddRef", "Release",
    "CreateDevice", "EnumDevices", "GetDeviceStatus",
    "RunControlPanel", "Initialize", "FindDevice", "CreateDeviceEx"
};

static uint32_t g_vtable;          /* guest address of the shared vtable */
static uint32_t g_object;          /* the single IDirectInput7 we hand out */

/* ---- the methods ------------------------------------------------------- */

static void m_QueryInterface(CPU *C)
{
    /* (this, riid, ppvObj). Everything the game asks us for is this same
       object; there is only one interface here. Setting *ppvObj matters more
       than the HRESULT -- callers read the pointer. */
    uint32_t ppv = A(2);
    if (ppv) WR32(ppv, THIS);
    ret_com(C, S_OK, 2);
}

static void m_AddRef(CPU *C)
{
    uint32_t n = RD32(THIS + 4u) + 1u;
    WR32(THIS + 4u, n);
    C->eax = n;
    C->esp += 4u + 4u;
}

static void m_Release(CPU *C)
{
    uint32_t n = RD32(THIS + 4u);
    if (n) n--;
    WR32(THIS + 4u, n);
    /* Deliberately not freed: the object is process-wide and both callers
       hold it. Freeing on the first Release would hand the second a dangling
       vtable pointer, and the fault would land in whichever of them polled
       first, looking like an input bug. */
    C->eax = n;
    C->esp += 4u + 4u;
}

/* DIDEVTYPE_*, the DirectInput 7 numbering (dwVersion 0x700). */
#define DIDEVTYPE_DEVICE   1
#define DIDEVTYPE_MOUSE    2
#define DIDEVTYPE_KEYBOARD 3
#define DIDEVTYPE_JOYSTICK 4

static const char *devtype_name(uint32_t t)
{
    switch (t & 0xffu) {
    case 0:                  return "ALL";
    case DIDEVTYPE_DEVICE:   return "DEVICE";
    case DIDEVTYPE_MOUSE:    return "MOUSE";
    case DIDEVTYPE_KEYBOARD: return "KEYBOARD";
    case DIDEVTYPE_JOYSTICK: return "JOYSTICK";
    default:                 return "(unknown)";
    }
}

/*
 * Reported once PER (devType, flags), not once in total.
 *
 * It used to be a single `static int told`, and that is a diagnostic that
 * cannot print what matters: the engine calls EnumDevices from two places with
 * different device classes, and only the first was ever named. "Reports zero
 * devices" then read as one missing list when it is several. The count is
 * carried too, so a caller that enumerates every frame is distinguishable from
 * one that enumerates at startup.
 */
#define ENUM_SEEN 8
static struct { uint32_t devtype, flags, cb; unsigned long n; int reported; }
    g_enum[ENUM_SEEN];
static int g_nenum;

/* Record one EnumDevices call and, the first time each distinct one is seen,
   say what it was ANSWERED with -- the count on its own cannot distinguish an
   enumeration that offered two devices from one that offered none. */
static void enum_seen(uint32_t devtype, uint32_t flags, uint32_t cb,
                      int reported)
{
    int i;
    for (i = 0; i < g_nenum; i++)
        if (g_enum[i].devtype == devtype && g_enum[i].flags == flags
            && g_enum[i].cb == cb) { g_enum[i].n++; return; }
    if (g_nenum == ENUM_SEEN) return;
    g_enum[g_nenum].devtype = devtype;
    g_enum[g_nenum].flags = flags;
    g_enum[g_nenum].cb = cb;
    g_enum[g_nenum].n = 1;
    g_enum[g_nenum].reported = reported;
    g_nenum++;
    {
        const char *nm = x86_native_name_at(cb);
        fprintf(stderr,
                "DINPUT: EnumDevices(devType=%u %s, flags=0x%x) offered %d "
                "device(s) to the callback at 0x%08x (%s).\n",
                devtype, devtype_name(devtype), flags, reported, cb,
                nm ? nm : "in no body this host can name");
        if (!reported)
            fprintf(stderr, "  NONE matched that device class. This host "
                            "serves the system keyboard and mouse only, so a "
                            "request for joysticks finds nothing -- and that "
                            "is a missing subsystem, not an empty machine. "
                            "See issue #32.\n");
    }
}

/*
 * A DIDEVICEINSTANCEA for one system device, in guest memory.
 *
 *   DWORD dwSize; GUID guidInstance; GUID guidProduct; DWORD dwDevType;
 *   CHAR tszInstanceName[260]; CHAR tszProductName[260]; GUID guidFFDriver;
 *   WORD wUsagePage; WORD wUsage;                                 -- 580 bytes
 *
 * dwSize is what tells DirectInput which VERSION of the structure the caller
 * expects: 580 is the DirectX 5-and-later form and 556 is the DirectX 3 one.
 * The game asks for interface version 0x700, so it reads the 580-byte form,
 * and writing the smaller one would leave it reading two names and a GUID out
 * of whatever followed the buffer.
 *
 * dwDevType's low byte is the type and its second byte the SUBTYPE. A subtype
 * of zero is not "unspecified" to a caller that switches on it -- the engine's
 * controller manager does -- so the traditional-mouse and enhanced-keyboard
 * subtypes are named rather than left at 0.
 */
#define DIDEVTYPEKEYBOARD_PCENH     3
#define DIDEVTYPEMOUSE_TRADITIONAL  1

static uint32_t devinst_for(int kind)
{
    static uint32_t buf;
    const unsigned char *guid = dinput_guid_of(kind);
    const char *name = kind == DINPUT_DEV_KEYBOARD ? "Keyboard" : "Mouse";
    uint32_t devtype = kind == DINPUT_DEV_KEYBOARD
                           ? (uint32_t)DIDEVTYPE_KEYBOARD |
                             ((uint32_t)DIDEVTYPEKEYBOARD_PCENH << 8)
                           : (uint32_t)DIDEVTYPE_MOUSE |
                             ((uint32_t)DIDEVTYPEMOUSE_TRADITIONAL << 8);

    if (!buf) buf = guest_malloc(580u);
    if (!buf || !guid) return 0;
    memset((void *)(uintptr_t)buf, 0, 580u);
    WR32(buf + 0u, 580u);                              /* dwSize */
    memcpy((void *)(uintptr_t)(buf + 4u), guid, 16);   /* guidInstance */
    memcpy((void *)(uintptr_t)(buf + 20u), guid, 16);  /* guidProduct */
    WR32(buf + 36u, devtype);
    snprintf((char *)(uintptr_t)(buf + 40u), 260, "%s", name);
    snprintf((char *)(uintptr_t)(buf + 300u), 260, "%s", name);
    return buf;
}

/*
 * EnumDevices, for real: the system keyboard and mouse.
 *
 * The callback is GUEST code -- `BOOL CALLBACK(LPCDIDEVICEINSTANCEA, LPVOID)`,
 * stdcall -- so enumerating is calling back into the game with a structure it
 * will read, and DIENUM_STOP (0) from it must stop the enumeration. A host
 * that ignored the return value would keep offering devices to a caller that
 * had already taken what it wanted.
 *
 * Only devices this host can actually SERVE are offered. Reporting a joystick
 * because SDL sees one would make the game create it, set a data format on it,
 * acquire it and poll it every frame, and every one of those has to work.
 */
static void m_EnumDevices(CPU *C)
{
    /* (this, dwDevType, lpCallback, pvRef, dwFlags) */
    uint32_t devtype = A(1), cb = A(2), pvref = A(3), flags = A(4);
    static const int KINDS[2] = { DINPUT_DEV_KEYBOARD, DINPUT_DEV_MOUSE };
    unsigned wanted = devtype & 0xffu;
    int reported = 0, i;

    if (!cb) { ret_com(C, DIERR_INVALIDPARAM, 4); return; }
    for (i = 0; i < 2; i++) {
        unsigned t = KINDS[i] == DINPUT_DEV_KEYBOARD ? DIDEVTYPE_KEYBOARD
                                                     : DIDEVTYPE_MOUSE;
        uint32_t inst;
        CPU K;
        if (wanted != 0 && wanted != t) continue;      /* 0 means ALL */
        if (!(inst = devinst_for(KINDS[i]))) continue;
        K = *C;
        K.esp -= 8u;
        WR32(K.esp + 0u, inst);
        WR32(K.esp + 4u, pvref);
        x86_guest_call(&K, cb);
        reported++;
        if (K.eax == 0u) break;                        /* DIENUM_STOP */
    }
    enum_seen(devtype, flags, cb, reported);
    ret_com(C, S_OK, 4);
}

/*
 * CreateDevice and CreateDeviceEx.
 *
 * CreateDeviceEx takes an extra riid between the GUID and the out-pointer and
 * is otherwise the same call; DirectInput 7 titles use either. Both are served
 * because the engine uses CreateDeviceEx and the exe uses CreateDevice, and a
 * host that implemented one would work for one of them and report
 * DIERR_DEVICENOTREG for the other with nothing to distinguish it from a
 * device that genuinely is not there.
 */
static void create_device(CPU *C, uint32_t guid, uint32_t out, uint32_t outer,
                          int nargs, const char *what)
{
    int kind;
    uint32_t obj;

    if (!out) { ret_com(C, DIERR_INVALIDPARAM, nargs); return; }
    WR32(out, 0);
    if (outer || !guid) { ret_com(C, DIERR_INVALIDPARAM, nargs); return; }
    if (!(kind = dinput_guid_kind(guid))) {
        const unsigned char *b = (const unsigned char *)(uintptr_t)guid;
        fprintf(stderr, "DINPUT: %s for {%02X%02X%02X%02X-...} -- not the "
                        "system keyboard or mouse, and this host enumerates "
                        "nothing else, so there is no device to open.\n",
                what, b[3], b[2], b[1], b[0]);
        ret_com(C, DIERR_DEVICENOTREG, nargs);
        return;
    }
    obj = dinput_device_new((DInputDeviceKind)kind);
    if (!obj) { ret_com(C, DIERR_OUTOFMEMORY, nargs); return; }
    {
        /* Once per kind: the engine creates each device once and then polls
           it, and a line per poll would bury everything else. A device that
           was ENUMERATED and never created is a different bug from one that
           was created and never read, so both ends are said. */
        static int told[3];
        if (!told[kind]++)
            fprintf(stderr, "DINPUT: %s handed the engine the system %s at "
                            "0x%08x -- the DirectInput 7 path now serves the "
                            "same devices as the DirectInput 8 one.\n",
                    what, kind == DINPUT_DEV_KEYBOARD ? "keyboard" : "mouse",
                    obj);
    }
    WR32(out, obj);
    ret_com(C, S_OK, nargs);
}

static void m_CreateDevice(CPU *C)
{
    /* (this, rguid, lplpDirectInputDevice, pUnkOuter) */
    create_device(C, A(1), A(2), A(3), 3, "CreateDevice");
}

static void m_CreateDeviceEx(CPU *C)
{
    /* (this, rguid, riid, ppvOut, pUnkOuter) */
    create_device(C, A(1), A(3), A(4), 4, "CreateDeviceEx");
}

void dinput_report(void)
{
    int i;
    if (!g_nenum) {
        printf("  dinput: EnumDevices was never called, so no device list was "
               "even asked for.\n");
        return;
    }
    printf("  dinput: %d distinct EnumDevices call(s):\n", g_nenum);
    for (i = 0; i < g_nenum; i++) {
        const char *nm = x86_native_name_at(g_enum[i].cb);
        printf("        devType %u %-9s flags 0x%-4x  x%-5lu  offered %d  "
               "callback 0x%08x %s\n", g_enum[i].devtype,
               devtype_name(g_enum[i].devtype), g_enum[i].flags, g_enum[i].n,
               g_enum[i].reported, g_enum[i].cb, nm ? nm : "");
    }
}

static void m_GetDeviceStatus(CPU *C)
{
    /* (this, rguidInstance) -- no device is present, and S_FALSE is what
       DirectInput returns for "not attached". */
    ret_com(C, S_FALSE, 1);
}

static void m_Initialize(CPU *C)
{
    /* (this, hinst, dwVersion) -- already initialised by construction. */
    ret_com(C, S_OK, 2);
}

static void m_RunControlPanel(CPU *C)
{
    /* (this, hwndOwner, dwFlags) -- there is no control panel to run. */
    ret_com(C, S_OK, 2);
}

/*
 * The rest. Reaching one means the game got past enumeration and wants a real
 * device, which cannot happen while EnumDevices reports none -- so if this
 * fires, the assumption above is wrong and that is worth knowing loudly.
 */
static void m_unimplemented(CPU *C)
{
    const char *nm = (const char *)x86_callback_ctx();
    fprintf(stderr,
            "\n*** DINPUT: IDirectInput7::%s was called, and is not "
            "implemented.\n"
            "    EnumDevices reports no devices, so nothing should have a "
            "device to ask about --\n"
            "    reaching this means that assumption is wrong. "
            "See src/native/dinput.c.\n",
            nm ? nm : "(unknown slot)");
    fflush(stderr);
    x86_diag_dump();
    abort();
    (void)C;
}

/* ---- construction ------------------------------------------------------ */

static void build(void)
{
    static void (*const impl[VT_COUNT])(CPU *) = {
        m_QueryInterface, m_AddRef, m_Release,
        m_CreateDevice,
        m_EnumDevices, m_GetDeviceStatus, m_RunControlPanel, m_Initialize,
        NULL,                  /* FindDevice */
        m_CreateDeviceEx
    };
    int k;
    if (g_object) return;

    g_vtable = guest_malloc(VT_COUNT * 4u);
    g_object = guest_malloc(8u);
    if (!g_vtable || !g_object) {
        fprintf(stderr, "DINPUT: no guest memory for the IDirectInput7 "
                        "object\n");
        abort();
    }
    for (k = 0; k < VT_COUNT; k++)
        WR32(g_vtable + (uint32_t)k * 4u,
             x86_native_callback(impl[k] ? impl[k] : m_unimplemented,
                                 "IDirectInput7", VT_NAME[k],
                                 (void *)VT_NAME[k]));
    WR32(g_object + 0u, g_vtable);
    WR32(g_object + 4u, 1u);          /* refcount */
}

void imp_DINPUT_DirectInputCreateEx(CPU *C)
{
    /* (hinst, dwVersion, riid, ppvOut, punkOuter) -- __stdcall, 5 args. */
    uint32_t version = A(1), ppv = A(3), outer = A(4);
    static int told;

    if (outer) {                       /* aggregation is not supported */
        if (ppv) WR32(ppv, 0);
        C->eax = DIERR_INVALIDPARAM;
        C->esp += 4u + 5u * 4u;
        return;
    }
    build();
    if (!told++)
        fprintf(stderr, "DINPUT: DirectInputCreateEx(version=0x%x) -> a native "
                        "IDirectInput7 at 0x%08x (SDL-backed input pending)\n",
                version, g_object);
    if (ppv) WR32(ppv, g_object);
    WR32(g_object + 4u, RD32(g_object + 4u) + 1u);
    C->eax = S_OK;
    C->esp += 4u + 5u * 4u;
}
