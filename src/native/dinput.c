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
 * WHAT THIS DOES NOT DO YET, said plainly because a silent zero here is
 * indistinguishable from working input: EnumDevices reports NO devices. The
 * object, its lifetime and the enumeration protocol are real; the device list
 * is empty, so the game starts with no keyboard, mouse or pad. Wiring it to
 * SDL3 is the next step, and src/display/ already has the controller backend.
 *
 * Every method that is reached but unimplemented aborts by NAME rather than
 * returning a plausible HRESULT, because DirectInput callers routinely ignore
 * return codes and read the out-parameter instead.
 */
#include "x86rt.h"
#include "x86rt_native.h"
#include "guest_heap.h"

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
static struct { uint32_t devtype, flags, cb; unsigned long n; } g_enum[ENUM_SEEN];
static int g_nenum;

static void m_EnumDevices(CPU *C)
{
    /* (this, dwDevType, lpCallback, pvRef, dwFlags) */
    uint32_t devtype = A(1), cb = A(2), flags = A(4);
    int i;

    for (i = 0; i < g_nenum; i++)
        if (g_enum[i].devtype == devtype && g_enum[i].flags == flags
            && g_enum[i].cb == cb) { g_enum[i].n++; break; }
    if (i == g_nenum && g_nenum < ENUM_SEEN) {
        const char *nm = x86_native_name_at(cb);
        g_enum[g_nenum].devtype = devtype;
        g_enum[g_nenum].flags = flags;
        g_enum[g_nenum].cb = cb;
        g_enum[g_nenum].n = 1;
        g_nenum++;
        fprintf(stderr,
                "DINPUT: EnumDevices(devType=%u %s, flags=0x%x) is reporting "
                "ZERO devices.\n"
                "  The enumeration protocol is real -- the callback at 0x%08x "
                "(%s) would be invoked once per device --\n"
                "  but no device list is wired up yet, so the game starts with "
                "no keyboard, mouse or pad.\n"
                "  This is a missing subsystem, not an empty machine. See "
                "src/native/dinput.c and issue #32.\n",
                devtype, devtype_name(devtype), flags, cb,
                nm ? nm : "in no body this host can name");
    }
    /* Enumerating nothing is a successful enumeration: DirectInput returns
       DI_OK and simply never calls the callback. */
    ret_com(C, S_OK, 4);
}

void dinput_report(void)
{
    int i;
    if (!g_nenum) {
        printf("  dinput: EnumDevices was never called, so no device list was "
               "even asked for.\n");
        return;
    }
    printf("  dinput: %d distinct EnumDevices call(s), all answered with ZERO "
           "devices:\n", g_nenum);
    for (i = 0; i < g_nenum; i++) {
        const char *nm = x86_native_name_at(g_enum[i].cb);
        printf("        devType %u %-9s flags 0x%-4x  x%-5lu  callback "
               "0x%08x %s\n", g_enum[i].devtype, devtype_name(g_enum[i].devtype),
               g_enum[i].flags, g_enum[i].n, g_enum[i].cb, nm ? nm : "");
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
        NULL,                  /* CreateDevice */
        m_EnumDevices, m_GetDeviceStatus, m_RunControlPanel, m_Initialize,
        NULL,                  /* FindDevice */
        NULL                   /* CreateDeviceEx */
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
