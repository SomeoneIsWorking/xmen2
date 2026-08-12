/*
 * The gamepad inventory: SDL3 on one side, DirectInput's idea of a joystick on
 * the other. See dinput_pad.h for why this is its own file.
 *
 * WHAT THE GAME ASKED FOR, read out of XMen2.exe rather than assumed. The
 * enumeration callback is FUN_00628b40 and it does, in order:
 *
 *   CreateDevice(guidInstance)          the GUID an enumeration handed it
 *   copy 100 bytes from instance+0x28   the instance NAME, kept per slot
 *   SetDataFormat(0x006a6514)           c_dfDIJoystick2: 272 bytes, 164 objects
 *   SetCooperativeLevel(hwnd, 5 or 6)   EXCLUSIVE or NONEXCLUSIVE | FOREGROUND
 *   EnumObjects(FUN_00628b20, ctx, 3)   DIDFT_AXIS -- once per axis
 *
 * and its per-axis callback (FUN_00628510) immediately calls
 * SetProperty(DIPROP_RANGE, {dwSize 0x18, dwHeaderSize 0x10, dwObj = the
 * object's dwType, dwHow = DIPH_BYID, lMin = -1000, lMax = +1000}).
 *
 * So the axis range is the GAME's, not DirectInput's default, and a host that
 * returned 0..65535 would hand it sticks pinned hard right. That is why
 * dinput_pad_axis takes the range it must produce.
 *
 * The same callback then checks the object's dwFlags for DIDOI_FFACTUATOR and,
 * for the first two axes that have it, remembers them and later builds a
 * DIEFFECT and calls CreateEffect. This host has no force feedback, so it
 * reports no actuator flag and that path is never entered -- which is a real
 * behaviour difference and is stated here rather than left to be discovered.
 *
 * WHAT A PAD LOOKS LIKE. It is presented as the DirectInput layout of an Xbox
 * 360 pad, because that is a layout the game already knows: the engine's own
 * controller-type enumeration has XBOX360_MICROSOFT_10BUTTONSPOV in it
 * (src/display/ig_controller.h, RE'd from libIGDisplay). Left stick on X/Y,
 * right stick on Rx/Ry, both triggers COMBINED on Z (left positive, right
 * negative -- the 360's actual DirectInput behaviour, not a simplification),
 * d-pad on POV 0, and ten buttons in the 360's order.
 */
#include "dinput_pad.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef X2_WITH_SDL
#include <SDL3/SDL.h>
#endif

typedef struct {
    int           used;
#ifdef X2_WITH_SDL
    SDL_Gamepad  *gp;
    SDL_JoystickID id;
#endif
    unsigned char inst[16];       /* instance GUID -- SDL's, which is stable */
    unsigned char prod[16];       /* product GUID -- the PIDVID form */
    char          name[128];
    int           buttons;
} Pad;

static Pad g_pad[DINPUT_PAD_MAX];
static int g_scanned;
static unsigned long g_opens, g_closes;

/*
 * The DirectInput product GUID for a USB device: {PIDVID-0000-0000-0000-
 * 504944564944}, whose first dword is (product << 16) | vendor and whose tail
 * is the ASCII "PIDVID". Windows builds it exactly this way, so a game that
 * recognises a pad by its product GUID -- which is how a 2005 title tells an
 * Xbox pad from a generic one -- sees what it would see on Windows.
 */
static void product_guid(unsigned char g[16], uint16_t vid, uint16_t pid)
{
    static const unsigned char TAIL[10] = {
        0x00,0x00, 0x00,0x00, 'P','I','D','V','I','D'
    };
    g[0] = (unsigned char)(vid & 0xff);
    g[1] = (unsigned char)(vid >> 8);
    g[2] = (unsigned char)(pid & 0xff);
    g[3] = (unsigned char)(pid >> 8);
    memcpy(g + 4, TAIL, 10);
    g[14] = 0x00;
    g[15] = 0x00;
}

#ifdef X2_WITH_SDL
static int slot_of_id(SDL_JoystickID id)
{
    int i;
    for (i = 0; i < DINPUT_PAD_MAX; i++)
        if (g_pad[i].used && g_pad[i].id == id) return i;
    return -1;
}

static void pad_open(SDL_JoystickID id)
{
    int i;
    SDL_Gamepad *gp;
    Pad *p;
    SDL_GUID gu;
    const char *nm;

    if (slot_of_id(id) >= 0) return;
    for (i = 0; i < DINPUT_PAD_MAX; i++) if (!g_pad[i].used) break;
    if (i == DINPUT_PAD_MAX) {
        fprintf(stderr, "DINPUT-PAD: a %d-th pad appeared and there is no slot "
                        "for it. The game supports four players; the limit here "
                        "is DINPUT_PAD_MAX in dinput_pad.h.\n", DINPUT_PAD_MAX + 1);
        return;
    }
    if (!(gp = SDL_OpenGamepad(id))) {
        fprintf(stderr, "DINPUT-PAD: SDL_OpenGamepad(%u) failed (%s); this pad "
                        "is NOT enumerated rather than enumerated and dead.\n",
                (unsigned)id, SDL_GetError());
        return;
    }
    p = &g_pad[i];
    memset(p, 0, sizeof *p);
    p->used = 1;
    p->gp = gp;
    p->id = id;
    /* SDL's joystick GUID identifies the DEVICE and survives a reconnect, so
       it is the right thing to hand out as the DirectInput instance GUID: the
       game keys its player slots on it (XMen2.exe keeps ten of them at
       this+0x27e8 and matches new arrivals against that table). */
    gu = SDL_GetJoystickGUIDForID(id);
    memcpy(p->inst, gu.data, 16);
    product_guid(p->prod, SDL_GetGamepadVendorForID(id),
                 SDL_GetGamepadProductForID(id));
    nm = SDL_GetGamepadNameForID(id);
    snprintf(p->name, sizeof p->name, "%s", nm ? nm : "Gamepad");
    /* Ten, the 360 layout the game knows. Reported rather than derived from
       SDL's button count: the DirectInput button ORDER is what matters and it
       is fixed by the mapping below, not by how many buttons SDL found. */
    p->buttons = 10;
    g_opens++;
    fprintf(stderr, "DINPUT-PAD: pad %d connected -- \"%s\" (vendor 0x%04x "
                    "product 0x%04x). Presented to the game as an Xbox 360 "
                    "DirectInput pad: 6 axes, 10 buttons, 1 POV.\n",
            i, p->name, SDL_GetGamepadVendorForID(id),
            SDL_GetGamepadProductForID(id));
}

static void pad_close(SDL_JoystickID id)
{
    int i = slot_of_id(id);
    if (i < 0) return;
    SDL_CloseGamepad(g_pad[i].gp);
    fprintf(stderr, "DINPUT-PAD: pad %d disconnected -- \"%s\".\n",
            i, g_pad[i].name);
    memset(&g_pad[i], 0, sizeof g_pad[i]);
    g_closes++;
}
#endif /* X2_WITH_SDL */

void dinput_pad_refresh(void)
{
#ifdef X2_WITH_SDL
    int n = 0, i, j;
    SDL_JoystickID *ids;

    if (!SDL_WasInit(SDL_INIT_GAMEPAD)) {
        if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
            /* Said ONCE, and it matters: with no gamepad subsystem every
               enumeration below reports zero pads, which is indistinguishable
               from a machine with no pad plugged in. */
            static int told;
            if (!told++)
                fprintf(stderr, "DINPUT-PAD: SDL_INIT_GAMEPAD failed (%s). NO "
                                "pad can be enumerated in this run -- that is "
                                "the subsystem missing, not an empty USB "
                                "port.\n", SDL_GetError());
            return;
        }
    }
    if (!(ids = SDL_GetGamepads(&n))) return;
    for (i = 0; i < n; i++) pad_open(ids[i]);
    /* And close anything SDL no longer lists. Done by rescan rather than by
       event so that this is correct however it is called -- an event-only path
       misses every pad that was already gone when the first poll happened. */
    for (i = 0; i < DINPUT_PAD_MAX; i++) {
        if (!g_pad[i].used) continue;
        for (j = 0; j < n; j++) if (ids[j] == g_pad[i].id) break;
        if (j == n) pad_close(g_pad[i].id);
    }
    SDL_free(ids);
    g_scanned = 1;
#endif
}

int dinput_pad_count(void)
{
    int i, n = 0;
    if (!g_scanned) dinput_pad_refresh();
    for (i = 0; i < DINPUT_PAD_MAX; i++) if (g_pad[i].used) n++;
    return n;
}

static Pad *pad_at(int pad)
{
    if (pad < 0 || pad >= DINPUT_PAD_MAX || !g_pad[pad].used) return NULL;
    return &g_pad[pad];
}

int dinput_pad_instance_guid(int pad, unsigned char guid[16])
{
    Pad *p = pad_at(pad);
    if (!p) return 0;
    memcpy(guid, p->inst, 16);
    return 1;
}

int dinput_pad_product_guid(int pad, unsigned char guid[16])
{
    Pad *p = pad_at(pad);
    if (!p) return 0;
    memcpy(guid, p->prod, 16);
    return 1;
}

const char *dinput_pad_name(int pad)
{
    Pad *p = pad_at(pad);
    return p ? p->name : NULL;
}

int dinput_pad_for_guid(const unsigned char guid[16])
{
    int i;
    for (i = 0; i < DINPUT_PAD_MAX; i++)
        if (g_pad[i].used && memcmp(g_pad[i].inst, guid, 16) == 0) return i;
    return -1;
}

int dinput_pad_button_count(int pad)
{
    Pad *p = pad_at(pad);
    return p ? p->buttons : 0;
}

#ifdef X2_WITH_SDL
/*
 * DirectInput button order for an Xbox 360 pad, which is the order the game's
 * own controller types are written against. Index is the DirectInput button
 * number; the value is the SDL gamepad button.
 */
static const SDL_GamepadButton BTN[10] = {
    SDL_GAMEPAD_BUTTON_SOUTH,          /* 0  A */
    SDL_GAMEPAD_BUTTON_EAST,           /* 1  B */
    SDL_GAMEPAD_BUTTON_WEST,           /* 2  X */
    SDL_GAMEPAD_BUTTON_NORTH,          /* 3  Y */
    SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,  /* 4  LB */
    SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, /* 5  RB */
    SDL_GAMEPAD_BUTTON_BACK,           /* 6  Back */
    SDL_GAMEPAD_BUTTON_START,          /* 7  Start */
    SDL_GAMEPAD_BUTTON_LEFT_STICK,     /* 8  LS */
    SDL_GAMEPAD_BUTTON_RIGHT_STICK     /* 9  RS */
};
#endif

int dinput_pad_button(int pad, int button)
{
#ifdef X2_WITH_SDL
    Pad *p = pad_at(pad);
    if (!p || button < 0 || button >= 10) return 0;
    return SDL_GetGamepadButton(p->gp, BTN[button]) ? 1 : 0;
#else
    (void)pad; (void)button;
    return 0;
#endif
}

int32_t dinput_pad_axis(int pad, int axis, int32_t lo, int32_t hi)
{
    /* Centred is the MIDPOINT of the range the game set, not zero: the game
       asked for [-1000, 1000] and would read a hard-left stick if a host that
       had been given [0, 65535] returned 0. */
    int32_t mid = lo + (hi - lo) / 2;
#ifdef X2_WITH_SDL
    Pad *p = pad_at(pad);
    int raw = 0;
    if (!p) return mid;
    switch (axis) {
    case DINPUT_PAD_AXIS_X:  raw = SDL_GetGamepadAxis(p->gp, SDL_GAMEPAD_AXIS_LEFTX);  break;
    case DINPUT_PAD_AXIS_Y:  raw = SDL_GetGamepadAxis(p->gp, SDL_GAMEPAD_AXIS_LEFTY);  break;
    case DINPUT_PAD_AXIS_RX: raw = SDL_GetGamepadAxis(p->gp, SDL_GAMEPAD_AXIS_RIGHTX); break;
    case DINPUT_PAD_AXIS_RY: raw = SDL_GetGamepadAxis(p->gp, SDL_GAMEPAD_AXIS_RIGHTY); break;
    case DINPUT_PAD_AXIS_Z: {
        /*
         * BOTH TRIGGERS ON ONE AXIS, and that is not a shortcut.
         *
         * A 360 pad on DirectInput reports its triggers as a single Z axis:
         * left drives it positive, right negative, and pressing both together
         * cancels. It is a well-known wart of that driver, and it is what a
         * 2005 game written against a 360 pad expects to read -- giving each
         * trigger its own axis here would be a DIFFERENT controller from the
         * one the game's mapping was written for.
         */
        int l = SDL_GetGamepadAxis(p->gp, SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
        int r = SDL_GetGamepadAxis(p->gp, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
        raw = (l - r) / 2;                    /* 0..32767 each -> -16383..16383 */
        break;
    }
    case DINPUT_PAD_AXIS_RZ: raw = 0; break;
    default: return mid;
    }
    /* SDL's -32768..32767 into the caller's range, with the midpoint exact. */
    if (raw < -32767) raw = -32767;
    return mid + (int32_t)((int64_t)raw * (hi - lo) / 2 / 32767);
#else
    (void)pad; (void)axis;
    return mid;
#endif
}

uint32_t dinput_pad_pov(int pad)
{
#ifdef X2_WITH_SDL
    Pad *p = pad_at(pad);
    int up, down, left, right;
    if (!p) return 0xFFFFFFFFu;
    up    = SDL_GetGamepadButton(p->gp, SDL_GAMEPAD_BUTTON_DPAD_UP);
    down  = SDL_GetGamepadButton(p->gp, SDL_GAMEPAD_BUTTON_DPAD_DOWN);
    left  = SDL_GetGamepadButton(p->gp, SDL_GAMEPAD_BUTTON_DPAD_LEFT);
    right = SDL_GetGamepadButton(p->gp, SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
    /* Hundredths of a degree clockwise from north, and CENTRED is
       0xFFFFFFFF -- not 0, which is north. A host that returned 0 for centred
       would hold "up" down for the whole run. */
    if (up   && !left && !right) return 0;
    if (up   && right)           return 4500;
    if (right && !up && !down)   return 9000;
    if (down && right)           return 13500;
    if (down && !left && !right) return 18000;
    if (down && left)            return 22500;
    if (left && !up && !down)    return 27000;
    if (up   && left)            return 31500;
    return 0xFFFFFFFFu;
#else
    (void)pad;
    return 0xFFFFFFFFu;
#endif
}

/*
 * X2_VIRTUAL_PAD -- attach a virtual gamepad, so a headless run has one.
 *
 * The same reasoning as X2_INPUT_SCRIPT for the keyboard: a headless run has
 * no hardware, so the entire controller path -- enumeration, CreateDevice,
 * the data format, the axis enumeration and the per-axis range the game sets
 * -- could never be exercised without a person plugging something in. This
 * gives it a pad to find.
 *
 * It is ANNOUNCED, loudly, for the same reason every injected key press is:
 * a run with a synthetic pad must not be mistakable for a run with a real one.
 */
void dinput_pad_virtual_from_env(void)
{
#ifdef X2_WITH_SDL
    const char *e = getenv("X2_VIRTUAL_PAD");
    SDL_VirtualJoystickDesc desc;
    SDL_JoystickID jid;
    SDL_GUID g;
    char gs[64], map[600];

    if (!e || !*e || *e == '0') return;
    if (!SDL_WasInit(SDL_INIT_GAMEPAD) && !SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
        fprintf(stderr, "DINPUT-PAD: X2_VIRTUAL_PAD is set but SDL's gamepad "
                        "subsystem would not start (%s). NO pad is attached.\n",
                SDL_GetError());
        return;
    }
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes = 6;
    desc.nbuttons = 11;
    desc.nhats = 1;
    if ((jid = SDL_AttachVirtualJoystick(&desc)) == 0) {
        fprintf(stderr, "DINPUT-PAD: X2_VIRTUAL_PAD is set but "
                        "SDL_AttachVirtualJoystick failed (%s). NO pad is "
                        "attached -- the run continues WITHOUT one rather than "
                        "pretending.\n", SDL_GetError());
        return;
    }
    g = SDL_GetJoystickGUIDForID(jid);
    SDL_GUIDToString(g, gs, sizeof gs);
    snprintf(map, sizeof map,
             "%s,X2 Virtual Pad,a:b0,b:b1,x:b2,y:b3,back:b4,start:b5,"
             "leftstick:b6,rightstick:b7,leftshoulder:b8,rightshoulder:b9,"
             "dpup:h0.1,dpright:h0.2,dpdown:h0.4,dpleft:h0.8,"
             "leftx:a0,lefty:a1,rightx:a2,righty:a3,"
             "lefttrigger:a4,righttrigger:a5,", gs);
    SDL_AddGamepadMapping(map);
    dinput_pad_refresh();
    fprintf(stderr, "DINPUT-PAD: X2_VIRTUAL_PAD -- a SYNTHETIC gamepad is "
                    "attached. Nothing in this run's controller behaviour came "
                    "from real hardware, and this line is here so that cannot "
                    "be mistaken.\n");
#endif
}

void dinput_pad_report(void)
{
    /* Once. Two endings call this -- atexit and the interrupt reports -- and
       neither covers every case, so both do it and this decides. */
    static int done;
    int i, n;
    if (done++) return;
    n = dinput_pad_count();
    /* Printed at zero too, with what that means: "no pad is plugged in" and
       "this host cannot see pads" are different facts and the second one is a
       defect. */
    if (!n) {
        printf("  gamepads: NONE connected%s\n",
               g_opens ? " now (some were, earlier in this run)"
                       : " and none ever was -- either nothing is plugged in, "
                         "or SDL's gamepad subsystem never came up (that is "
                         "reported by name when it happens)");
        return;
    }
    printf("  gamepads: %d connected (%lu connect(s), %lu disconnect(s) this "
           "run)\n", n, g_opens, g_closes);
    for (i = 0; i < DINPUT_PAD_MAX; i++)
        if (g_pad[i].used)
            printf("         pad %d  \"%s\"  %d button(s), presented as an "
                   "Xbox 360 DirectInput pad\n", i, g_pad[i].name,
                   g_pad[i].buttons);
}
