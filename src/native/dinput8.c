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
#include "dinput_device.h"
#include "dinput_pad.h"

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
#define DIERR_DEVICENOTREG 0x80040154u
#define DIERR_OUTOFMEMORY  0x8007000Eu

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
static struct { uint32_t cls, flags, cb; unsigned long n; int reported; }
    g_enum[ENUM_SEEN];
static int g_nenum;

/*
 * Recorded once per (class, flags, callback), WITH how many devices it was
 * offered.
 *
 * The count is the whole point. "EnumDevices was called" and "EnumDevices
 * found nothing" are different facts, and for most of this port's life the
 * answer was zero -- printed with its reason so that it could not be mistaken
 * for a machine with no pad plugged in.
 */
static void enum_seen(uint32_t cls, uint32_t flags, uint32_t cb, int reported)
{
    int i;
    for (i = 0; i < g_nenum; i++)
        if (g_enum[i].cls == cls && g_enum[i].flags == flags
            && g_enum[i].cb == cb) { g_enum[i].n++; return; }
    if (g_nenum == ENUM_SEEN) return;
    g_enum[g_nenum].cls = cls;
    g_enum[g_nenum].flags = flags;
    g_enum[g_nenum].cb = cb;
    g_enum[g_nenum].n = 1;
    g_enum[g_nenum].reported = reported;
    g_nenum++;
    {
        const char *nm = x86_native_name_at(cb);
        if (reported > 0)
            fprintf(stderr, "DINPUT8: EnumDevices(class=%u %s, flags=0x%x) "
                            "offered %d device(s) to the callback at 0x%08x "
                            "(%s).\n", cls, devclass_name(cls), flags,
                    reported, cb, nm ? nm : "in no body this host can name");
        else
            fprintf(stderr, "DINPUT8: EnumDevices(class=%u %s, flags=0x%x) "
                            "found NO device to offer.\n"
                            "  The protocol works -- the callback at 0x%08x "
                            "(%s) would run once per device -- so this is an "
                            "empty device list, not a missing one. For a "
                            "gamepad class, plug one in; see "
                            "src/native/dinput_pad.c.\n",
                    cls, devclass_name(cls), flags, cb,
                    nm ? nm : "in no body this host can name");
    }
}

/*
 * DIDEVICEINSTANCEA for a gamepad, in the DirectInput 8 form.
 *
 * The size the caller sees decides which form it reads: 0x244 (580) is the
 * DirectX 5-and-later structure with both names and the FF GUID, and the
 * game's callback (XMen2.exe FUN_00628b40) reads the instance GUID at +4 and
 * copies 100 bytes of the instance NAME from +0x28, so both have to be there
 * and in those places.
 */
#define DIDEVINST_BYTES 580u

static uint32_t padinst_for(int pad)
{
    static uint32_t buf;
    unsigned char inst[16], prod[16];
    const char *nm = dinput_pad_name(pad);

    if (!dinput_pad_instance_guid(pad, inst)) return 0;
    if (!dinput_pad_product_guid(pad, prod))  return 0;
    if (!buf) buf = guest_malloc(DIDEVINST_BYTES);
    if (!buf) return 0;
    memset((void *)(uintptr_t)buf, 0, DIDEVINST_BYTES);
    WR32(buf + 0u, DIDEVINST_BYTES);                     /* dwSize */
    memcpy((void *)(uintptr_t)(buf + 4u),  inst, 16);    /* guidInstance */
    memcpy((void *)(uintptr_t)(buf + 20u), prod, 16);    /* guidProduct */
    /* DI8DEVTYPE_GAMEPAD with DI8DEVTYPEGAMEPAD_STANDARD in the second byte,
       and DIDEVTYPE_HID (0x00010000) set: a caller that switches on the
       subtype gets a real one rather than zero. */
    WR32(buf + 36u, 0x00010115u);
    snprintf((char *)(uintptr_t)(buf + 40u),  260, "%s", nm ? nm : "Gamepad");
    snprintf((char *)(uintptr_t)(buf + 300u), 260, "%s", nm ? nm : "Gamepad");
    return buf;
}

/*
 * EnumDevices, for real.
 *
 * This answered ZERO for the whole life of the port, and that single answer is
 * what kept every controller feature out of reach: the exe asks for
 * DI8DEVCLASS_GAMECTRL once at startup (FUN_00628e20), and with nothing
 * offered it builds no controllers, so nothing downstream -- hotswap, mapping,
 * button prompts -- has anything to attach to.
 *
 * Only devices this host can actually SERVE are offered. That is not caution
 * for its own sake: reporting a pad makes the game create it, set a data
 * format, enumerate its axes, set a range on each, acquire it and read it
 * every frame, and every one of those has to work or the game gets a device
 * that exists and never reports a press.
 */
/*
 * The game's own gamepad enumeration, remembered so a pad plugged in LATER can
 * be handed to it.
 *
 * XMen2.exe enumerates game controllers exactly once, at startup (there is no
 * WM_DEVICECHANGE anywhere in it), so a pad connected after that is invisible
 * for the rest of the run. But its enumeration callback is not a one-shot: it
 * finds a free slot, creates the device, configures it and marks it attached,
 * which is precisely what a new arrival needs. So hotswap here is not a new
 * mechanism bolted on -- it is the game's own callback, called again.
 *
 * pvRef is the input manager itself: the shim at FUN_00628e00 reads it from
 * [ESP+8] and passes it as `this` to FUN_00628b40.
 */
static uint32_t g_pad_cb, g_pad_ref, g_pad_enum;
static unsigned char g_offered[DINPUT_PAD_MAX][16];
static int g_noffered;
static unsigned long g_hotplug_offers;

static int already_offered(const unsigned char guid[16])
{
    int i;
    for (i = 0; i < g_noffered; i++)
        if (memcmp(g_offered[i], guid, 16) == 0) return 1;
    return 0;
}

static void note_offered(const unsigned char guid[16])
{
    if (already_offered(guid) || g_noffered >= DINPUT_PAD_MAX) return;
    memcpy(g_offered[g_noffered++], guid, 16);
}

static void m_EnumDevices(CPU *C)
{
    /* (this, dwDevType, lpCallback, pvRef, dwFlags) */
    uint32_t cls = A(1), cb = A(2), pvref = A(3), flags = A(4);
    int reported = 0, i, npad;

    if (!cb) { ret_com(C, DIERR_INVALIDPARAM, 4); return; }
    dinput_pad_refresh();
    npad = dinput_pad_count();
    if (cls == DI8DEVCLASS_GAMECTRL) {
        /*
         * Remember the game's own RE-ENUMERATION ROUTINE, found by asking which
         * function this call came from rather than by hardcoding its address.
         *
         * XMen2.exe's FUN_00628e20 takes one BOOL argument, stores it at
         * `this+2`, clears the attached flags at `this+0x4e4`, calls
         * EnumDevices(GAMECTRL, ...) and clears the flag again on the way out.
         * That flag is what its per-device callback checks before recording a
         * controller's GUID in the ten-slot table at `this+0x27e8` -- and a
         * device whose GUID is not in that table is created and then never
         * stored, so the game never polls it. That is exactly what a hotswap
         * that called the per-device callback directly produced: the pad was
         * created and configured, and read zero times.
         *
         * So hotswap calls THIS, with the same argument startup uses. No host
         * code writes guest state; the game admits the controller by its own
         * rules.
         */
        const char *nm = NULL;
        uint32_t here = x86_native_entry_containing(RD32(C->esp), &nm);
        g_pad_cb = cb;
        g_pad_ref = pvref;
        if (here && here != g_pad_enum) {
            g_pad_enum = here;
            fprintf(stderr, "DINPUT8: the game's gamepad enumeration routine is "
                            "0x%08x (%s) -- hotswap will call THAT when a pad "
                            "arrives, so a late controller is admitted by the "
                            "game's own rules.\n", here, nm ? nm : "unnamed");
        }
    }

    /* DI8DEVCLASS_ALL is 0. GAMECTRL is the only class with anything in it
       here; the keyboard and mouse are reached by their fixed GUIDs and this
       game never enumerates them through DirectInput 8. */
    if (cls == 0u || cls == DI8DEVCLASS_GAMECTRL) {
        for (i = 0; i < DINPUT_PAD_MAX && reported < npad; i++) {
            uint32_t inst = padinst_for(i);
            CPU K;
            if (!inst) continue;
            K = *C;
            K.esp -= 8u;
            WR32(K.esp + 0u, inst);
            WR32(K.esp + 4u, pvref);
            x86_guest_call_args(&K, cb, 8u);
            reported++;
            { unsigned char g[16];
              if (dinput_pad_instance_guid(i, g)) note_offered(g); }
            if (K.eax == 0u) break;                 /* DIENUM_STOP */
        }
    }
    enum_seen(cls, flags, cb, reported);
    ret_com(C, S_OK, 4);
}

/*
 * HOTSWAP: offer the game any pad that has appeared since it enumerated.
 *
 * Called once a frame from the first input call of the frame (the keyboard's
 * GetDeviceState -- XMen2.exe's per-frame update FUN_006285c0 reads it at
 * 0x0062861e before anything else), so the guest is between operations rather
 * than in the middle of its own device loop.
 *
 * This RE-ENTERS the guest, which is the same thing the enumeration itself
 * does, and is why the pump point matters: the callback creates a device,
 * sets its data format and enumerates its axes, all of which come back through
 * this host. Doing it from inside the joystick loop would be inserting a
 * device into a table the game is walking.
 *
 * A pad is offered ONCE. The game keys its player slots on the instance GUID
 * and would otherwise be handed the same controller every frame.
 */
void dinput8_hotplug_pump(CPU *C)
{
    int i, fresh = 0;
    unsigned char g[16];

    if (!C || !g_pad_ref) return;
    dinput_pad_refresh();
    for (i = 0; i < DINPUT_PAD_MAX; i++)
        if (dinput_pad_instance_guid(i, g) && !already_offered(g)) fresh++;
    if (!fresh) return;

    if (!g_pad_enum) {
        static int told;
        if (!told++)
            fprintf(stderr, "DINPUT8: a pad appeared, and this host never "
                            "identified the game's enumeration routine, so it "
                            "cannot be offered. The pad is CONNECTED and the "
                            "game will not see it.\n");
        return;
    }
    for (i = 0; i < DINPUT_PAD_MAX; i++)
        if (dinput_pad_instance_guid(i, g) && !already_offered(g))
            fprintf(stderr, "DINPUT8: HOTSWAP -- pad %d (\"%s\") appeared after "
                            "the game had enumerated. Calling the game's own "
                            "enumeration routine at 0x%08x, exactly as startup "
                            "does; nothing here creates a controller behind the "
                            "game's back.\n",
                    i, dinput_pad_name(i) ? dinput_pad_name(i) : "?", g_pad_enum);
    g_hotplug_offers++;
    {
        /* __thiscall FUN_00628e20(BOOL bRecordNew): ECX = the input manager,
           one stack argument. TRUE is what admits a controller the game has
           not seen before -- the same value startup passes. */
        CPU K = *C;
        K.ecx = g_pad_ref;
        K.esp -= 4u;
        WR32(K.esp, 1u);
        x86_guest_call_args(&K, g_pad_enum, 4u);
    }
    /* Whatever it took, it has now been offered: the enumeration above ran and
       recorded every connected pad. Marking them here rather than inside the
       enumeration keeps this true even if the game declined one. */
    for (i = 0; i < DINPUT_PAD_MAX; i++)
        if (dinput_pad_instance_guid(i, g)) note_offered(g);
}

/*
 * The two system devices, recognised by GUID.
 *
 * GUID_SysKeyboard {6F1D2B61-D5A0-11CF-BFC7-444553540000} and GUID_SysMouse
 * {...2B60...} differ only in the first dword, which is why all sixteen bytes
 * are compared: matching on the first four would make every DirectInput GUID
 * in that family look like a keyboard.
 *
 * Verified against XMen2.exe's own data at 0x6a15e4 and 0x6a15f4 -- the exact
 * pointers FUN_00628e20 passes to CreateDevice -- rather than taken on trust
 * from a header.
 */
static const unsigned char GUID_SYS_KEYBOARD[16] = {
    0x61,0x2B,0x1D,0x6F, 0xA0,0xD5, 0xCF,0x11,
    0xBF,0xC7, 0x44,0x45,0x53,0x54,0x00,0x00
};
static const unsigned char GUID_SYS_MOUSE[16] = {
    0x60,0x2B,0x1D,0x6F, 0xA0,0xD5, 0xCF,0x11,
    0xBF,0xC7, 0x44,0x45,0x53,0x54,0x00,0x00
};

static void guid_text(uint32_t g, char *out, size_t n)
{
    const unsigned char *b = (const unsigned char *)(uintptr_t)g;
    snprintf(out, n, "{%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-"
                     "%02X%02X%02X%02X%02X%02X}",
             b[3], b[2], b[1], b[0], b[5], b[4], b[7], b[6], b[8], b[9],
             b[10], b[11], b[12], b[13], b[14], b[15]);
}

static void m_CreateDevice(CPU *C)
{
    /* (this, rguid, lplpDirectInputDevice, pUnkOuter) */
    uint32_t guid = A(1), out = A(2), outer = A(3);
    uint32_t obj = 0;

    if (!out) { ret_com(C, DIERR_INVALIDPARAM, 3); return; }
    WR32(out, 0);
    if (outer || !guid) { ret_com(C, DIERR_INVALIDPARAM, 3); return; }

    if (memcmp((const void *)(uintptr_t)guid, GUID_SYS_KEYBOARD, 16) == 0)
        obj = dinput_device_new(DINPUT_DEV_KEYBOARD);
    else if (memcmp((const void *)(uintptr_t)guid, GUID_SYS_MOUSE, 16) == 0)
        obj = dinput_device_new(DINPUT_DEV_MOUSE);
    else if (dinput_pad_for_guid((const unsigned char *)(uintptr_t)guid) >= 0) {
        /* A GUID the enumeration above handed out. A device enumerated under
           one GUID and creatable only under another is a device the game can
           see and never open, so the two go through the same inventory. */
        obj = dinput_device_new_pad(
                  dinput_pad_for_guid((const unsigned char *)(uintptr_t)guid));
    } else {
        /*
         * NOT a system device, so it is one that enumeration would have had to
         * produce -- and enumeration reports none. DIERR_DEVICENOTREG is the
         * truthful answer and the caller checks it (FUN_00628e20 tests the
         * HRESULT with JL before touching the pointer).
         *
         * Named, because "a device this host does not have" and "a GUID this
         * host failed to recognise" are the same return value and different
         * bugs.
         */
        char t[64];
        guid_text(guid, t, sizeof t);
        fprintf(stderr, "DINPUT8: CreateDevice(%s) -- not the system keyboard "
                        "or mouse, and no enumerated device can match it "
                        "because EnumDevices reports none.\n", t);
        ret_com(C, DIERR_DEVICENOTREG, 3);
        return;
    }
    if (!obj) { ret_com(C, DIERR_OUTOFMEMORY, 3); return; }
    WR32(out, obj);
    ret_com(C, S_OK, 3);
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
        m_CreateDevice,
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
