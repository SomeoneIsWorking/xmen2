/*
 * IDirectInputDevice8 for the system keyboard and mouse, backed by SDL3.
 *
 * The game asks for these two BY FIXED GUID -- GUID_SysKeyboard and
 * GUID_SysMouse, read straight out of XMen2.exe at 0x6a15e4 and 0x6a15f4 -- so
 * they need no enumeration and no device list. That is why they are the first
 * devices this host implements: they are the ones the run actually stops on
 * (issue #32), and they are reachable without solving the joystick enumeration
 * that pads will need.
 *
 * The sequence the exe performs, read from FUN_00628e20:
 *
 *     CreateDevice(GUID_SysKeyboard, &dev, NULL)
 *     dev->SetDataFormat(c_dfDIKeyboard)        [vtable + 0x2c]  slot 11
 *     dev->SetCooperativeLevel(hwnd, 0x16)      [vtable + 0x34]  slot 13
 *     dev->Acquire()                            [vtable + 0x1c]  slot 7
 *
 * and the same for the mouse with c_dfDIMouse and level 6. Those offsets are
 * what fix the slot numbering below; it is not taken from a header.
 *
 * THE STATE SIZE IS NOT ASSUMED. SetDataFormat is handed the game's own
 * DIDATAFORMAT and this reads dwDataSize out of it, so GetDeviceState fills
 * exactly what the caller's format says -- measured, the mouse format is 20
 * bytes over 11 objects, which is DIMOUSESTATE2 (8 buttons) and NOT the
 * 16-byte DIMOUSESTATE the name c_dfDIMouse suggests. Hardcoding 16 would have
 * written short and left the last four buttons as whatever was on the stack.
 *
 * WHAT IS HONEST ABOUT THE NEGATIVE: with no SDL video subsystem there is no
 * keyboard to read, and every key would read as up -- which is exactly what a
 * working keyboard with nothing pressed looks like. That case says so once, by
 * name, and is counted, rather than returning a quiet block of zeros.
 */
#include "x86rt.h"
#include "x86rt_native.h"
#include "guest_heap.h"
#include "dinput_device.h"
#include "win32_sdl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef X2_WITH_SDL
#include <SDL3/SDL.h>
#endif

#define A(i) RD32(C->esp + 4u + (uint32_t)(i) * 4u)
#define THIS A(0)

static void ret_com(CPU *C, uint32_t hr, int nargs)
{
    C->eax = hr;
    C->esp += 4u + (uint32_t)(nargs + 1) * 4u;
}

#define S_OK              0x00000000u
#define S_FALSE           0x00000001u
#define DI_NOEFFECT       0x00000001u
#define DIERR_INVALIDPARAM 0x80070057u
#define DIERR_NOTACQUIRED  0x8007000Cu

/*
 * IDirectInputDevice8's vtable. The four the game dispatches through are
 * pinned by the offsets above; the rest follow DirectInput 8's published
 * order. A wrong entry here would silently run the wrong method, so this is
 * the one place the numbering is written down.
 */
enum {
    VT_QueryInterface, VT_AddRef, VT_Release,
    VT_GetCapabilities, VT_EnumObjects, VT_GetProperty, VT_SetProperty,
    VT_Acquire, VT_Unacquire, VT_GetDeviceState, VT_GetDeviceData,
    VT_SetDataFormat, VT_SetEventNotification, VT_SetCooperativeLevel,
    VT_GetObjectInfo, VT_GetDeviceInfo, VT_RunControlPanel, VT_Initialize,
    VT_CreateEffect, VT_EnumEffects, VT_GetEffectInfo,
    VT_GetForceFeedbackState, VT_SendForceFeedbackCommand,
    VT_EnumCreatedEffectObjects, VT_Escape, VT_Poll, VT_SendDeviceData,
    VT_EnumEffectsInFile, VT_WriteEffectToFile, VT_BuildActionMap,
    VT_SetActionMap, VT_GetImageInfo,
    VT_COUNT
};

static const char *const VT_NAME[VT_COUNT] = {
    "QueryInterface", "AddRef", "Release",
    "GetCapabilities", "EnumObjects", "GetProperty", "SetProperty",
    "Acquire", "Unacquire", "GetDeviceState", "GetDeviceData",
    "SetDataFormat", "SetEventNotification", "SetCooperativeLevel",
    "GetObjectInfo", "GetDeviceInfo", "RunControlPanel", "Initialize",
    "CreateEffect", "EnumEffects", "GetEffectInfo",
    "GetForceFeedbackState", "SendForceFeedbackCommand",
    "EnumCreatedEffectObjects", "Escape", "Poll", "SendDeviceData",
    "EnumEffectsInFile", "WriteEffectToFile", "BuildActionMap",
    "SetActionMap", "GetImageInfo"
};

typedef struct {
    DInputDeviceKind kind;
    uint32_t guest;              /* the object the game holds */
    uint32_t refs;
    uint32_t data_size;          /* from the caller's own DIDATAFORMAT */
    uint32_t coop;
    int      acquired;
    unsigned long polls;
} Device;

#define MAX_DEVICES 4
static Device g_dev[MAX_DEVICES];
static int g_ndev;
static uint32_t g_vtable;

static Device *dev_of(uint32_t guest)
{
    int i;
    for (i = 0; i < g_ndev; i++) if (g_dev[i].guest == guest) return &g_dev[i];
    return NULL;
}

static const char *kind_name(DInputDeviceKind k)
{
    return k == DINPUT_DEV_KEYBOARD ? "keyboard"
         : k == DINPUT_DEV_MOUSE    ? "mouse" : "(unknown)";
}

/* ---- the SDL side ------------------------------------------------------ */

/*
 * SDL scancode -> DIK_*, which is the PS/2 set-1 scancode DirectInput indexes
 * its 256-byte keyboard block by. The two numberings are unrelated (SDL's is
 * USB HID usage), so this table is the whole mapping and a key missing from it
 * is a key the game can never see.
 *
 * Written out rather than computed: there is no formula, and a partial mapping
 * that looks like a formula is how half a keyboard goes missing quietly.
 */
#ifdef X2_WITH_SDL
static const struct { int sdl; unsigned char dik; } DIK_MAP[] = {
    { SDL_SCANCODE_ESCAPE, 0x01 },
    { SDL_SCANCODE_1, 0x02 }, { SDL_SCANCODE_2, 0x03 }, { SDL_SCANCODE_3, 0x04 },
    { SDL_SCANCODE_4, 0x05 }, { SDL_SCANCODE_5, 0x06 }, { SDL_SCANCODE_6, 0x07 },
    { SDL_SCANCODE_7, 0x08 }, { SDL_SCANCODE_8, 0x09 }, { SDL_SCANCODE_9, 0x0A },
    { SDL_SCANCODE_0, 0x0B },
    { SDL_SCANCODE_MINUS, 0x0C }, { SDL_SCANCODE_EQUALS, 0x0D },
    { SDL_SCANCODE_BACKSPACE, 0x0E }, { SDL_SCANCODE_TAB, 0x0F },
    { SDL_SCANCODE_Q, 0x10 }, { SDL_SCANCODE_W, 0x11 }, { SDL_SCANCODE_E, 0x12 },
    { SDL_SCANCODE_R, 0x13 }, { SDL_SCANCODE_T, 0x14 }, { SDL_SCANCODE_Y, 0x15 },
    { SDL_SCANCODE_U, 0x16 }, { SDL_SCANCODE_I, 0x17 }, { SDL_SCANCODE_O, 0x18 },
    { SDL_SCANCODE_P, 0x19 },
    { SDL_SCANCODE_LEFTBRACKET, 0x1A }, { SDL_SCANCODE_RIGHTBRACKET, 0x1B },
    { SDL_SCANCODE_RETURN, 0x1C }, { SDL_SCANCODE_LCTRL, 0x1D },
    { SDL_SCANCODE_A, 0x1E }, { SDL_SCANCODE_S, 0x1F }, { SDL_SCANCODE_D, 0x20 },
    { SDL_SCANCODE_F, 0x21 }, { SDL_SCANCODE_G, 0x22 }, { SDL_SCANCODE_H, 0x23 },
    { SDL_SCANCODE_J, 0x24 }, { SDL_SCANCODE_K, 0x25 }, { SDL_SCANCODE_L, 0x26 },
    { SDL_SCANCODE_SEMICOLON, 0x27 }, { SDL_SCANCODE_APOSTROPHE, 0x28 },
    { SDL_SCANCODE_GRAVE, 0x29 }, { SDL_SCANCODE_LSHIFT, 0x2A },
    { SDL_SCANCODE_BACKSLASH, 0x2B },
    { SDL_SCANCODE_Z, 0x2C }, { SDL_SCANCODE_X, 0x2D }, { SDL_SCANCODE_C, 0x2E },
    { SDL_SCANCODE_V, 0x2F }, { SDL_SCANCODE_B, 0x30 }, { SDL_SCANCODE_N, 0x31 },
    { SDL_SCANCODE_M, 0x32 },
    { SDL_SCANCODE_COMMA, 0x33 }, { SDL_SCANCODE_PERIOD, 0x34 },
    { SDL_SCANCODE_SLASH, 0x35 }, { SDL_SCANCODE_RSHIFT, 0x36 },
    { SDL_SCANCODE_KP_MULTIPLY, 0x37 }, { SDL_SCANCODE_LALT, 0x38 },
    { SDL_SCANCODE_SPACE, 0x39 }, { SDL_SCANCODE_CAPSLOCK, 0x3A },
    { SDL_SCANCODE_F1, 0x3B }, { SDL_SCANCODE_F2, 0x3C }, { SDL_SCANCODE_F3, 0x3D },
    { SDL_SCANCODE_F4, 0x3E }, { SDL_SCANCODE_F5, 0x3F }, { SDL_SCANCODE_F6, 0x40 },
    { SDL_SCANCODE_F7, 0x41 }, { SDL_SCANCODE_F8, 0x42 }, { SDL_SCANCODE_F9, 0x43 },
    { SDL_SCANCODE_F10, 0x44 },
    { SDL_SCANCODE_NUMLOCKCLEAR, 0x45 }, { SDL_SCANCODE_SCROLLLOCK, 0x46 },
    { SDL_SCANCODE_KP_7, 0x47 }, { SDL_SCANCODE_KP_8, 0x48 },
    { SDL_SCANCODE_KP_9, 0x49 }, { SDL_SCANCODE_KP_MINUS, 0x4A },
    { SDL_SCANCODE_KP_4, 0x4B }, { SDL_SCANCODE_KP_5, 0x4C },
    { SDL_SCANCODE_KP_6, 0x4D }, { SDL_SCANCODE_KP_PLUS, 0x4E },
    { SDL_SCANCODE_KP_1, 0x4F }, { SDL_SCANCODE_KP_2, 0x50 },
    { SDL_SCANCODE_KP_3, 0x51 }, { SDL_SCANCODE_KP_0, 0x52 },
    { SDL_SCANCODE_KP_PERIOD, 0x53 },
    { SDL_SCANCODE_F11, 0x57 }, { SDL_SCANCODE_F12, 0x58 },
    { SDL_SCANCODE_KP_ENTER, 0x9C }, { SDL_SCANCODE_RCTRL, 0x9D },
    { SDL_SCANCODE_KP_DIVIDE, 0xB5 }, { SDL_SCANCODE_RALT, 0xB8 },
    { SDL_SCANCODE_HOME, 0xC7 }, { SDL_SCANCODE_UP, 0xC8 },
    { SDL_SCANCODE_PAGEUP, 0xC9 }, { SDL_SCANCODE_LEFT, 0xCB },
    { SDL_SCANCODE_RIGHT, 0xCD }, { SDL_SCANCODE_END, 0xCF },
    { SDL_SCANCODE_DOWN, 0xD0 }, { SDL_SCANCODE_PAGEDOWN, 0xD1 },
    { SDL_SCANCODE_INSERT, 0xD2 }, { SDL_SCANCODE_DELETE, 0xD3 },
    { SDL_SCANCODE_LGUI, 0xDB }, { SDL_SCANCODE_RGUI, 0xDC },
    { SDL_SCANCODE_APPLICATION, 0xDD }
};
#define DIK_MAP_N ((int)(sizeof DIK_MAP / sizeof DIK_MAP[0]))
#endif

/* Said once: a keyboard nobody can read and a keyboard with nothing pressed
   produce the same 256 zero bytes, and only one of them is a missing
   subsystem. */
static unsigned long g_blind_reads;

static int input_available(void)
{
#ifdef X2_WITH_SDL
    return SDL_WasInit(SDL_INIT_VIDEO) != 0;
#else
    return 0;
#endif
}

static void say_blind(const char *what)
{
    if (g_blind_reads++) return;
    fprintf(stderr,
            "DINPUT8: the %s state was read with no SDL video subsystem up, so "
            "it reads as NOTHING PRESSED.\n"
            "  That is indistinguishable from a working device nobody is "
            "touching, which is why it is said here rather than left as a "
            "block of zeros.\n"
            "  Reported once; the total is in the exit report.\n", what);
}

static void fill_keyboard(uint32_t out, uint32_t n)
{
    memset((void *)(uintptr_t)out, 0, n);
    if (!input_available()) { say_blind("keyboard"); return; }
#ifdef X2_WITH_SDL
    {
        int nkeys = 0, i;
        const bool *keys;
        SDL_PumpEvents();
        keys = SDL_GetKeyboardState(&nkeys);
        if (!keys) { say_blind("keyboard"); return; }
        for (i = 0; i < DIK_MAP_N; i++) {
            if (DIK_MAP[i].sdl >= nkeys) continue;
            if (!keys[DIK_MAP[i].sdl]) continue;
            if ((uint32_t)DIK_MAP[i].dik >= n) continue;
            /* DirectInput marks a key down with the HIGH bit, not with 1. */
            *((unsigned char *)(uintptr_t)out + DIK_MAP[i].dik) = 0x80;
        }
    }
#endif
}

static void fill_mouse(uint32_t out, uint32_t n)
{
    /* DIMOUSESTATE(2): LONG lX, lY, lZ then one byte per button. The axes are
       RELATIVE -- deltas since the last read -- which is why SDL's relative
       state is the right source and the absolute position is not. */
    memset((void *)(uintptr_t)out, 0, n);
    if (!input_available()) { say_blind("mouse"); return; }
#ifdef X2_WITH_SDL
    {
        float dx = 0.0f, dy = 0.0f;
        SDL_MouseButtonFlags b;
        uint32_t nbuttons = n > 12u ? n - 12u : 0u;
        SDL_PumpEvents();
        b = SDL_GetRelativeMouseState(&dx, &dy);
        if (n >= 4u)  WR32(out + 0u, (uint32_t)(int32_t)dx);
        if (n >= 8u)  WR32(out + 4u, (uint32_t)(int32_t)dy);
        /* lZ is the wheel, which SDL reports as an event rather than a state;
           it stays 0 until the event pump keeps a running total. */
        if (nbuttons > 0u && (b & SDL_BUTTON_LMASK))
            *((unsigned char *)(uintptr_t)out + 12) = 0x80;
        if (nbuttons > 1u && (b & SDL_BUTTON_RMASK))
            *((unsigned char *)(uintptr_t)out + 13) = 0x80;
        if (nbuttons > 2u && (b & SDL_BUTTON_MMASK))
            *((unsigned char *)(uintptr_t)out + 14) = 0x80;
    }
#endif
}

/* ---- the methods ------------------------------------------------------- */

static void m_QueryInterface(CPU *C)
{
    uint32_t ppv = A(2);
    if (ppv) WR32(ppv, THIS);
    ret_com(C, S_OK, 2);
}

static void m_AddRef(CPU *C)
{
    Device *d = dev_of(THIS);
    uint32_t n = d ? ++d->refs : 1u;
    C->eax = n;
    C->esp += 4u + 4u;
}

static void m_Release(CPU *C)
{
    Device *d = dev_of(THIS);
    uint32_t n = 0;
    if (d && d->refs) n = --d->refs;
    /* Not destroyed at zero: the two system devices live for the process and
       more than one caller holds each. Freeing would hand a survivor a
       dangling vtable, and the fault would look like an input bug. */
    C->eax = n;
    C->esp += 4u + 4u;
}

static void m_SetDataFormat(CPU *C)
{
    /* (this, lpdf) -- the caller's own DIDATAFORMAT:
       dwSize, dwObjSize, dwFlags, dwDataSize, dwNumObjs, rgodf. */
    Device *d = dev_of(THIS);
    uint32_t df = A(1);
    if (!d || !df) { ret_com(C, DIERR_INVALIDPARAM, 1); return; }
    d->data_size = RD32(df + 12u);
    if (!d->data_size) {
        fprintf(stderr, "DINPUT8: SetDataFormat on the %s declared a zero-byte "
                        "state. Refusing: GetDeviceState would then fill "
                        "nothing and the game would read its own stack.\n",
                kind_name(d->kind));
        ret_com(C, DIERR_INVALIDPARAM, 1);
        return;
    }
    fprintf(stderr, "DINPUT8: the %s data format is %u byte(s) over %u "
                    "object(s).\n", kind_name(d->kind), d->data_size,
            RD32(df + 16u));
    ret_com(C, S_OK, 1);
}

static void m_SetCooperativeLevel(CPU *C)
{
    /* (this, hwnd, dwFlags). Nothing here can honour exclusivity -- SDL owns
       the window and this host does not steal the device from the desktop --
       and the game does not ask for it: the levels it passes are 0x16 and 6,
       both NONEXCLUSIVE. An EXCLUSIVE request would be a behaviour difference
       worth naming, so it is. */
    Device *d = dev_of(THIS);
    uint32_t flags = A(2);
    if (d) d->coop = flags;
    if (flags & 0x1u)
        fprintf(stderr, "DINPUT8: SetCooperativeLevel asked for EXCLUSIVE "
                        "access to the %s (flags 0x%x); this host cannot take "
                        "the device from the desktop, so it stays shared.\n",
                d ? kind_name(d->kind) : "device", flags);
    ret_com(C, S_OK, 2);
}

static void m_Acquire(CPU *C)
{
    Device *d = dev_of(THIS);
    if (!d) { ret_com(C, DIERR_INVALIDPARAM, 0); return; }
    if (d->acquired) { ret_com(C, S_FALSE, 0); return; }  /* already acquired */
    if (!d->data_size) {
        /* Real DirectInput refuses this, and so must we: the size the state
           will be written at is not known yet. */
        fprintf(stderr, "DINPUT8: Acquire on the %s before SetDataFormat.\n",
                kind_name(d->kind));
        ret_com(C, DIERR_INVALIDPARAM, 0);
        return;
    }
    d->acquired = 1;
    ret_com(C, S_OK, 0);
}

static void m_Unacquire(CPU *C)
{
    Device *d = dev_of(THIS);
    if (d) d->acquired = 0;
    ret_com(C, S_OK, 0);
}

static void m_GetDeviceState(CPU *C)
{
    /* (this, cbData, lpvData) */
    Device *d = dev_of(THIS);
    uint32_t cb = A(1), out = A(2);

    if (!d || !out) { ret_com(C, DIERR_INVALIDPARAM, 2); return; }
    if (!d->acquired) { ret_com(C, DIERR_NOTACQUIRED, 2); return; }
    if (cb != d->data_size) {
        /* Not clamped. A caller asking for a size its own data format did not
           declare is a disagreement about the layout, and filling the smaller
           of the two would hand back a state whose fields are in the wrong
           places. */
        fprintf(stderr, "DINPUT8: GetDeviceState on the %s asked for %u bytes "
                        "but its data format declared %u. Refusing rather than "
                        "writing a state whose fields land somewhere else.\n",
                kind_name(d->kind), cb, d->data_size);
        ret_com(C, DIERR_INVALIDPARAM, 2);
        return;
    }
    d->polls++;
    if (d->kind == DINPUT_DEV_KEYBOARD) fill_keyboard(out, cb);
    else                                fill_mouse(out, cb);
    ret_com(C, S_OK, 2);
}

static void m_GetDeviceData(CPU *C)
{
    /* (this, cbObjectData, rgdod, pdwInOut, dwFlags) -- BUFFERED input, which
       needs DIPROP_BUFFERSIZE to have been set. It has not been, so DirectInput
       itself would fail here; reporting zero events without saying so would
       look like an idle frame forever. */
    Device *d = dev_of(THIS);
    uint32_t inout = A(3);
    static unsigned long told;
    if (!told++)
        fprintf(stderr, "DINPUT8: GetDeviceData (buffered input) on the %s -- "
                        "no buffer was ever configured, so this reports ZERO "
                        "events, every time. If the game relies on buffered "
                        "keys rather than GetDeviceState, that is the next "
                        "work item.\n", d ? kind_name(d->kind) : "device");
    if (inout) WR32(inout, 0);
    ret_com(C, S_OK, 5);
}

static void m_GetCapabilities(CPU *C)
{
    /* (this, lpDIDevCaps): dwSize, dwFlags, dwDevType, dwAxes, dwButtons,
       dwPOVs, then force-feedback timings. */
    Device *d = dev_of(THIS);
    uint32_t caps = A(1);
    if (!caps || !d) { ret_com(C, DIERR_INVALIDPARAM, 1); return; }
    WR32(caps + 4u, 0x00000001u);                /* DIDC_ATTACHED */
    if (d->kind == DINPUT_DEV_KEYBOARD) {
        WR32(caps + 8u, 0x00000103u);            /* KEYBOARD | HID-less */
        WR32(caps + 12u, 0);                     /* axes */
        WR32(caps + 16u, 256);                   /* buttons */
    } else {
        WR32(caps + 8u, 0x00000102u);            /* MOUSE */
        WR32(caps + 12u, 3);
        WR32(caps + 16u, 8);
    }
    WR32(caps + 20u, 0);                         /* POVs */
    ret_com(C, S_OK, 1);
}

static void m_SetProperty(CPU *C)
{
    /*
     * (this, rguidProp, pdiph). Accepted and IGNORED is not a safe default in
     * general -- DIPROP_BUFFERSIZE changes what GetDeviceData must do, and
     * DIPROP_AXISMODE changes whether the mouse axes are absolute -- so the
     * ones that would change behaviour are named when they arrive rather than
     * swallowed. rguidProp is a small integer cast to a pointer for the
     * predefined properties, which is what makes this readable at all.
     */
    uint32_t prop = A(1);
    static unsigned long told;
    if (prop < 0x10000u && !told++)
        fprintf(stderr, "DINPUT8: SetProperty(#%u) is accepted and has NO "
                        "effect here. Buffer size (#1) and axis mode (#2) would "
                        "change what GetDeviceData and GetDeviceState must "
                        "return; see src/native/dinput_device.c.\n", prop);
    ret_com(C, S_OK, 2);
}

static void m_Poll(CPU *C)
{
    /* Nothing here is polled asynchronously -- the state is read live from SDL
       at GetDeviceState -- so there is genuinely nothing to do, which is what
       DI_NOEFFECT says. */
    ret_com(C, DI_NOEFFECT, 0);
}

static void m_unimplemented(CPU *C)
{
    const char *nm = (const char *)x86_callback_ctx();
    fprintf(stderr,
            "\n*** DINPUT8: IDirectInputDevice8::%s was called, and is not "
            "implemented.\n"
            "    The keyboard and mouse are served; this is the method the "
            "game wants NEXT, and that name IS the work item.\n"
            "    See src/native/dinput_device.c and issue #32.\n",
            nm ? nm : "(unknown slot)");
    fflush(stderr);
    x86_diag_dump();
    abort();
    (void)C;
}

/* ---- construction ------------------------------------------------------ */

static void build_vtable(void)
{
    static void (*const impl[VT_COUNT])(CPU *) = {
        m_QueryInterface, m_AddRef, m_Release,
        m_GetCapabilities,
        NULL,                        /* EnumObjects */
        NULL,                        /* GetProperty */
        m_SetProperty,
        m_Acquire, m_Unacquire, m_GetDeviceState, m_GetDeviceData,
        m_SetDataFormat,
        NULL,                        /* SetEventNotification */
        m_SetCooperativeLevel,
        NULL,                        /* GetObjectInfo */
        NULL,                        /* GetDeviceInfo */
        NULL,                        /* RunControlPanel */
        NULL,                        /* Initialize */
        NULL, NULL, NULL, NULL, NULL, NULL, NULL,   /* effects, Escape */
        m_Poll,
        NULL, NULL, NULL, NULL, NULL, NULL          /* the DX8 tail */
    };
    int k;
    if (g_vtable) return;
    g_vtable = guest_malloc(VT_COUNT * 4u);
    if (!g_vtable) {
        fprintf(stderr, "DINPUT8: no guest memory for the device vtable\n");
        abort();
    }
    for (k = 0; k < VT_COUNT; k++)
        WR32(g_vtable + (uint32_t)k * 4u,
             x86_native_callback(impl[k] ? impl[k] : m_unimplemented,
                                 "IDirectInputDevice8", VT_NAME[k],
                                 (void *)VT_NAME[k]));
}

uint32_t dinput_device_new(DInputDeviceKind kind)
{
    Device *d;
    uint32_t obj;
    int i;

    /* One object per kind, for the whole process: the game creates the system
       keyboard once and caches it, and handing out a second would give the two
       holders different acquire states. */
    for (i = 0; i < g_ndev; i++)
        if (g_dev[i].kind == kind) { g_dev[i].refs++; return g_dev[i].guest; }
    if (g_ndev == MAX_DEVICES) {
        fprintf(stderr, "DINPUT8: no room for another device\n");
        return 0;
    }
    build_vtable();
    obj = guest_malloc(8u);
    if (!obj) { fprintf(stderr, "DINPUT8: no guest memory for a device\n"); return 0; }
    WR32(obj + 0u, g_vtable);
    WR32(obj + 4u, 0);
    d = &g_dev[g_ndev++];
    memset(d, 0, sizeof *d);
    d->kind = kind;
    d->guest = obj;
    d->refs = 1;
    fprintf(stderr, "DINPUT8: a native %s device at 0x%08x%s\n",
            kind_name(kind), obj,
            input_available() ? " (SDL-backed)"
                              : " -- with no SDL video subsystem up, so it will "
                                "report nothing pressed");
    return obj;
}

void dinput_device_report(void)
{
    int i;
    if (!g_ndev) {
        printf("  dinput devices: none was ever created.\n");
        return;
    }
    printf("  dinput devices:\n");
    for (i = 0; i < g_ndev; i++)
        printf("        %-9s %u byte state, %s, %lu state read(s)\n",
               kind_name(g_dev[i].kind), g_dev[i].data_size,
               g_dev[i].acquired ? "acquired" : "NOT acquired", g_dev[i].polls);
    if (g_blind_reads)
        printf("        %lu of those read a device with no SDL video "
               "subsystem up, and reported nothing pressed.\n", g_blind_reads);
}
