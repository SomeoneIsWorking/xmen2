/*
 * DirectInput 8, as the EXE reaches it: not through an import, but by name at
 * run time.
 *
 * XMen2.exe builds "<system32>\dinput8.dll" from GetSystemDirectoryA, loads it,
 * and looks up DirectInput8Create (FUN_00626bf0). Nothing imports either, so
 * neither the IAT binder nor x86_native_thunk could ever have answered -- see
 * x86_native_export, which exists for exactly this shape.
 *
 * WHY THIS IS NOT COSMETIC. The game checks the pointer and handles NULL, and
 * the handling is to give up on input ENTIRELY: FUN_00629210 returns false, and
 * its caller FUN_0061bae0 jumps over the whole construction of the 5x4
 * controller table at 0x00a68f40. Nothing then fills it, and the first thing to
 * index it -- FUN_0061a810, which reads entry 0, adds 0x18 and dereferences it
 * -- faults at 0x18. That was issue #32, and it read as a renderer-adjacent
 * crash for as long as nobody followed the branch.
 *
 * This file is the IDirectInput8 half only: the object, its lifetime, and the
 * enumeration protocol. It reports NO devices, exactly as src/native/dinput.c
 * does for DirectInput 7, and says so per device class rather than once --
 * because the engine enumerates mice, keyboards and joysticks separately and a
 * single "reported zero devices" line hides two thirds of what is missing.
 *
 * Every method that is reached and not written aborts by NAME. DirectInput
 * callers routinely ignore HRESULTs and read the out-parameter instead, so a
 * plausible failure code would be indistinguishable from working.
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
#define DIERR_INVALIDPARAM 0x80070057u

/*
 * The IDirectInput8 vtable, in order.
 *
 * IUnknown, then IDirectInput8's own eight. It is NOT the DirectInput 7 layout
 * with two appended: EnumDevicesBySemantics and ConfigureDevices are new, and
 * getting the tail wrong would dispatch a call to the wrong body silently --
 * which is why the numbering is written down once, here, and the game's own
 * dispatch through [vtable+0x0c] for CreateDevice confirms slot 3.
 */
enum {
    VT_QueryInterface, VT_AddRef, VT_Release,
    VT_CreateDevice, VT_EnumDevices, VT_GetDeviceStatus,
    VT_RunControlPanel, VT_Initialize,
    VT_FindDevice, VT_EnumDevicesBySemantics, VT_ConfigureDevices,
    VT_COUNT
};

static const char *const VT_NAME[VT_COUNT] = {
    "QueryInterface", "AddRef", "Release",
    "CreateDevice", "EnumDevices", "GetDeviceStatus",
    "RunControlPanel", "Initialize", "FindDevice",
    "EnumDevicesBySemantics", "ConfigureDevices"
};

/* DI8DEVCLASS_* -- the DirectInput 8 numbering, which is NOT DIDEVTYPE_*. */
#define DI8DEVCLASS_ALL        0
#define DI8DEVCLASS_DEVICE     1
#define DI8DEVCLASS_POINTER    2
#define DI8DEVCLASS_KEYBOARD   3
#define DI8DEVCLASS_GAMECTRL   4

static const char *devclass_name(uint32_t t)
{
    switch (t) {
    case DI8DEVCLASS_ALL:      return "ALL";
    case DI8DEVCLASS_DEVICE:   return "DEVICE";
    case DI8DEVCLASS_POINTER:  return "POINTER";
    case DI8DEVCLASS_KEYBOARD: return "KEYBOARD";
    case DI8DEVCLASS_GAMECTRL: return "GAMECTRL";
    default:                   return "(device type, not a class)";
    }
}

static uint32_t g_vtable, g_object;
static unsigned long g_creates;

/* ---- the methods ------------------------------------------------------- */

static void m_QueryInterface(CPU *C)
{
    /* (this, riid, ppvObj). There is one interface here and every riid the
       game asks for is answered with it; callers read the pointer, not the
       HRESULT. */
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
    /* Not freed at zero, for the reason dinput.c gives: the object is
       process-wide, more than one caller holds it, and handing the second a
       dangling vtable pointer would fault somewhere that looks like input. */
    C->eax = n;
    C->esp += 4u + 4u;
}

/*
 * Reported once per (device class, flags, callback), with a count.
 *
 * A single `told` flag would name whichever class was enumerated first and
 * leave the others invisible, and the classes are enumerated from different
 * places for different purposes -- so "zero devices" is not one gap but one
 * per line below.
 */
#define ENUM_SEEN 8
static struct { uint32_t cls, flags, cb; unsigned long n; } g_enum[ENUM_SEEN];
static int g_nenum;

static void m_EnumDevices(CPU *C)
{
    /* (this, dwDevType, lpCallback, pvRef, dwFlags) */
    uint32_t cls = A(1), cb = A(2), flags = A(4);
    int i;

    for (i = 0; i < g_nenum; i++)
        if (g_enum[i].cls == cls && g_enum[i].flags == flags
            && g_enum[i].cb == cb) { g_enum[i].n++; break; }
    if (i == g_nenum && g_nenum < ENUM_SEEN) {
        const char *nm = x86_native_name_at(cb);
        g_enum[g_nenum].cls = cls;
        g_enum[g_nenum].flags = flags;
        g_enum[g_nenum].cb = cb;
        g_enum[g_nenum].n = 1;
        g_nenum++;
        fprintf(stderr,
                "DINPUT8: EnumDevices(class=%u %s, flags=0x%x) is reporting "
                "ZERO devices.\n"
                "  The protocol is real -- the callback at 0x%08x (%s) would "
                "run once per device --\n"
                "  but no device list is wired up yet. See src/native/dinput8.c "
                "and issue #32; src/display/ has the SDL3 backend this should "
                "be fed from.\n",
                cls, devclass_name(cls), flags, cb,
                nm ? nm : "in no body this host can name");
    }
    /* Enumerating nothing IS a successful enumeration: DirectInput returns
       DI_OK and simply never calls the callback. */
    ret_com(C, S_OK, 4);
}

static void m_GetDeviceStatus(CPU *C)
{
    /* (this, rguidInstance) -- S_FALSE is DirectInput's "not attached". */
    ret_com(C, S_FALSE, 1);
}

static void m_RunControlPanel(CPU *C)
{
    /* (this, hwndOwner, dwFlags) -- there is no control panel to run. */
    ret_com(C, S_OK, 2);
}

static void m_Initialize(CPU *C)
{
    /* (this, hinst, dwVersion) -- already initialised by construction. */
    ret_com(C, S_OK, 2);
}

static void m_unimplemented(CPU *C)
{
    const char *nm = (const char *)x86_callback_ctx();
    fprintf(stderr,
            "\n*** DINPUT8: IDirectInput8::%s was called, and is not "
            "implemented.\n"
            "    EnumDevices reports no devices, so nothing should have a "
            "device to ask about --\n"
            "    reaching this means the game wants one anyway, and THAT is "
            "the work item.\n"
            "    See src/native/dinput8.c and issue #32.\n",
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
        NULL,                       /* CreateDevice */
        m_EnumDevices, m_GetDeviceStatus, m_RunControlPanel, m_Initialize,
        NULL,                       /* FindDevice */
        NULL,                       /* EnumDevicesBySemantics */
        NULL                        /* ConfigureDevices */
    };
    int k;
    if (g_object) return;

    g_vtable = guest_malloc(VT_COUNT * 4u);
    g_object = guest_malloc(8u);
    if (!g_vtable || !g_object) {
        fprintf(stderr, "DINPUT8: no guest memory for the IDirectInput8 "
                        "object\n");
        abort();
    }
    for (k = 0; k < VT_COUNT; k++)
        WR32(g_vtable + (uint32_t)k * 4u,
             x86_native_callback(impl[k] ? impl[k] : m_unimplemented,
                                 "IDirectInput8", VT_NAME[k],
                                 (void *)VT_NAME[k]));
    WR32(g_object + 0u, g_vtable);
    WR32(g_object + 4u, 1u);          /* refcount */
}

/*
 * DirectInput8Create(hinst, dwVersion, riid, ppvOut, punkOuter) -- __stdcall.
 *
 * The game does not check the HRESULT (FUN_00629210 calls it and moves on), so
 * what matters is the OUT-POINTER: it writes the object into the field it will
 * dispatch through for the rest of the run.
 */
void imp_DINPUT8_DirectInput8Create(CPU *C)
{
    uint32_t version = A(1), ppv = A(3), outer = A(4);

    if (outer) {                        /* aggregation is not supported */
        if (ppv) WR32(ppv, 0);
        C->eax = DIERR_INVALIDPARAM;
        C->esp += 4u + 5u * 4u;
        return;
    }
    build();
    if (!g_creates++)
        fprintf(stderr, "DINPUT8: DirectInput8Create(version=0x%x) -> a native "
                        "IDirectInput8 at 0x%08x. Input is no longer disabled "
                        "wholesale; the device list is still empty.\n",
                version, g_object);
    if (ppv) WR32(ppv, g_object);
    WR32(g_object + 4u, RD32(g_object + 4u) + 1u);
    C->eax = S_OK;
    C->esp += 4u + 5u * 4u;
}

void dinput8_install(void)
{
    x86_native_export("DINPUT8.DLL", "DirectInput8Create",
                      imp_DINPUT8_DirectInput8Create);
}

void dinput8_report(void)
{
    int i;
    if (!g_creates) {
        printf("  dinput8: DirectInput8Create was never called.\n");
        return;
    }
    printf("  dinput8: %lu DirectInput8Create call(s)", g_creates);
    if (!g_nenum) {
        printf(", and EnumDevices was never reached.\n");
        return;
    }
    printf("; %d distinct EnumDevices call(s), all answered with ZERO "
           "devices:\n", g_nenum);
    for (i = 0; i < g_nenum; i++) {
        const char *nm = x86_native_name_at(g_enum[i].cb);
        printf("        class %u %-9s flags 0x%-4x  x%-5lu  callback "
               "0x%08x %s\n", g_enum[i].cls, devclass_name(g_enum[i].cls),
               g_enum[i].flags, g_enum[i].n, g_enum[i].cb, nm ? nm : "");
    }
}
