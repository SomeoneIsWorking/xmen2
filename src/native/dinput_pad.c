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
#include "guest_clock.h"

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
    int           xbox_glyphs;
} Pad;

static Pad g_pad[DINPUT_PAD_MAX];
static int g_scanned;
static unsigned long g_opens, g_closes;

int dinput_pad_type_uses_xbox_glyphs(int type)
{
#ifdef X2_WITH_SDL
    return type == SDL_GAMEPAD_TYPE_XBOX360 ||
           type == SDL_GAMEPAD_TYPE_XBOXONE;
#else
    (void)type;
    return 0;
#endif
}

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
    p->xbox_glyphs = dinput_pad_type_uses_xbox_glyphs(
        (int)SDL_GetGamepadType(gp));
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

int dinput_pad_uses_xbox_glyphs(int pad)
{
    Pad *p = pad_at(pad);
    return p ? p->xbox_glyphs : 0;
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

/*
 * Does the game ever ASK, and does the answer ever come back pressed?
 *
 * "The controller does nothing" has three causes that look identical from
 * outside: the game never polls the pad, it polls and SDL reports nothing, or
 * it sees the button and the screen in front of you ignores it. Without a
 * denominator all three read as silence, so these count every read and every
 * read that came back DOWN, and the pair is reported whether or not either is
 * zero -- "0 of 0" and "0 of 480,000" are completely different findings.
 */
static unsigned long g_btn_reads, g_btn_down, g_axis_reads, g_axis_offcentre;
static unsigned long g_pad_pumps;

/*
 * Refresh SDL's view of the pads, ONCE per device poll.
 *
 * SDL_GetGamepadButton and SDL_GetGamepadAxis report the state SDL last
 * latched; nothing refreshes it but SDL_UpdateGamepads (which SDL_PumpEvents
 * calls in turn). The keyboard and mouse paths in dinput_system.c have always
 * pumped before reading -- the pad path never did, so every button read came
 * back released no matter what the hardware was doing. Measured: 67,420 button
 * reads in one run, 0 of them down, with a press held across thousands of
 * them.
 *
 * Called from dinput_joystick_state, not from the per-button read: the game
 * asks for ten buttons and six axes per poll, and pumping sixteen times a
 * frame would be doing the same work sixteen times over.
 */
void dinput_pad_refresh_state(void)
{
#ifdef X2_WITH_SDL
    g_pad_pumps++;
    SDL_UpdateGamepads();
#endif
}

int dinput_pad_button(int pad, int button)
{
#ifdef X2_WITH_SDL
    Pad *p = pad_at(pad);
    int down;
    g_btn_reads++;
    if (!p || button < 0 || button >= 10) return 0;
    down = SDL_GetGamepadButton(p->gp, BTN[button]) ? 1 : 0;
    if (down) g_btn_down++;
    return down;
#else
    (void)pad; (void)button;
    return 0;
#endif
}

int32_t dinput_pad_axis(int pad, int axis, int32_t lo, int32_t hi)
{
    g_axis_reads++;
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
    if (raw) g_axis_offcentre++;
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
/*
 * The synthetic pad's buttons, in JOYSTICK index order. This is the single
 * definition: the SDL mapping string is generated from it, and a name asked
 * for over the control channel is looked up in it. See virtual_attach.
 */
#define VBTN_N 10
static const char *const g_vbtn_name[VBTN_N] = {
    "a", "b", "x", "y", "back", "start",
    "leftstick", "rightstick", "leftshoulder", "rightshoulder"
};

/* A press held until `until` (guest seconds), so a press survives the game's
   per-frame poll the way a real thumb does. 0 = not held. */
static double g_vbtn_until[VBTN_N];
static double g_vaxis_until[6];
static short  g_vaxis_value[6];
static unsigned long g_vpad_presses, g_vpad_axis_sets;

static int  g_virt_at_frame = -1;         /* fN form: attach at that frame */
static SDL_JoystickID g_virt_id;
static SDL_Joystick  *g_virt_js;   /* opened so its axes/buttons can be set */
static int  g_virt_detach_at = -1;

static void virtual_attach(void);
static void virtual_expire(void);

void dinput_pad_virtual_from_env(void)
{
#ifdef X2_WITH_SDL
    const char *e = getenv("X2_VIRTUAL_PAD");

    if (!e || !*e || *e == '0') return;
    /*
     * X2_VIRTUAL_PAD=1              attach now
     * X2_VIRTUAL_PAD=f2000          attach at frame 2000
     * X2_VIRTUAL_PAD=f2000-3000     attach at 2000, UNPLUG at 3000
     *
     * The frame forms are what make HOTSWAP testable: a pad present from the
     * start proves only that startup enumeration works, and the whole point of
     * hotswap is the pad that arrives after the game has stopped looking.
     * Frames rather than seconds for the reason X2_INPUT_SCRIPT uses them --
     * a script written against the wall clock is written against one machine.
     */
    if (*e == 'f') {
        char *end;
        g_virt_at_frame = (int)strtol(e + 1, &end, 10);
        if (*end == '-') g_virt_detach_at = (int)strtol(end + 1, NULL, 10);
        fprintf(stderr, "DINPUT-PAD: X2_VIRTUAL_PAD -- a SYNTHETIC gamepad will "
                        "be attached at frame %d%s. Nothing in this run's "
                        "controller behaviour comes from real hardware.\n",
                g_virt_at_frame,
                g_virt_detach_at >= 0 ? " and unplugged again later" : "");
        return;
    }
    virtual_attach();
#endif
}

/*
 * Called once a frame, so the fN forms above can fire. Cheap: an int compare
 * until the frame arrives.
 */
void dinput_pad_virtual_tick(unsigned long frame)
{
#ifdef X2_WITH_SDL
    virtual_expire();
    if (g_virt_at_frame >= 0 && frame >= (unsigned long)g_virt_at_frame) {
        g_virt_at_frame = -1;
        virtual_attach();
    }
    if (g_virt_detach_at >= 0 && frame >= (unsigned long)g_virt_detach_at) {
        g_virt_detach_at = -1;
        if (g_virt_id) {
            fprintf(stderr, "DINPUT-PAD: X2_VIRTUAL_PAD -- UNPLUGGING the "
                            "synthetic pad at frame %lu.\n", frame);
            if (g_virt_js) { SDL_CloseJoystick(g_virt_js); g_virt_js = NULL; }
            SDL_DetachVirtualJoystick(g_virt_id);
            g_virt_id = 0;
            dinput_pad_refresh();
        }
    }
#else
    (void)frame;
#endif
}

#ifdef X2_WITH_SDL
static void virtual_attach(void)
{
    SDL_VirtualJoystickDesc desc;
    SDL_JoystickID jid;
    SDL_GUID g;
    char gs[64], map[600];

    if (!SDL_WasInit(SDL_INIT_GAMEPAD) && !SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
        fprintf(stderr, "DINPUT-PAD: X2_VIRTUAL_PAD is set but SDL's gamepad "
                        "subsystem would not start (%s). NO pad is attached.\n",
                SDL_GetError());
        return;
    }
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    /* A deterministic Xbox 360 identity makes the synthetic device exercise
       the same prompt-family path as the hardware it models. */
    desc.vendor_id = 0x045e;
    desc.product_id = 0x028e;
    desc.name = "X2 Virtual Xbox 360 Pad";
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
    {
        /*
         * The mapping string is BUILT FROM g_vbtn_name, not written beside it.
         *
         * A virtual joystick is driven by JOYSTICK button index, while the
         * game-facing names are GAMEPAD buttons, and the two orders differ:
         * this mapping puts start at b5, where SDL's own enum has GUIDE at 5
         * and START at 6. Written out twice, the pair drifts and every press
         * lands one button off -- which looks exactly like "the controller
         * does nothing in this menu". One table, two readers.
         */
        size_t n = 0;
        int i;
        n += (size_t)snprintf(map + n, sizeof map - n, "%s,X2 Virtual Pad,", gs);
        for (i = 0; i < VBTN_N; i++)
            n += (size_t)snprintf(map + n, sizeof map - n, "%s:b%d,",
                                  g_vbtn_name[i], i);
        snprintf(map + n, sizeof map - n,
                 "dpup:h0.1,dpright:h0.2,dpdown:h0.4,dpleft:h0.8,"
                 "leftx:a0,lefty:a1,rightx:a2,righty:a3,"
                 "lefttrigger:a4,righttrigger:a5,");
    }
    {
        /*
         * What SDL actually did with it. A mapping that is silently rejected
         * leaves the pad enumerating perfectly and reading UP forever, which
         * is indistinguishable from a game that ignores the pad -- so the
         * return code and the mapping SDL ENDS UP USING are both reported,
         * not just the fact that a mapping was offered.
         */
        int rc = SDL_AddGamepadMapping(map);
        fprintf(stderr, "DINPUT-PAD: mapping offered (%zu bytes) -> %s\n",
                strlen(map),
                rc < 0 ? SDL_GetError() : (rc ? "added" : "updated existing"));
    }
    g_virt_id = jid;
    g_virt_js = SDL_OpenJoystick(jid);
    if (!g_virt_js)
        fprintf(stderr, "DINPUT-PAD: the synthetic pad attached but could NOT be "
                        "opened (%s), so nothing can press its buttons. It will "
                        "enumerate and read as all-zero forever.\n", SDL_GetError());
    dinput_pad_refresh();
    {
        char *m = SDL_GetGamepadMappingForID(jid);
        fprintf(stderr, "DINPUT-PAD: SDL_IsGamepad=%d; the mapping IN FORCE is: "
                        "%s\n", (int)SDL_IsGamepad(jid), m ? m : "(none)");
        if (m) SDL_free(m);
    }
    fprintf(stderr, "DINPUT-PAD: attached on thread %llu\n",
            (unsigned long long)SDL_GetCurrentThreadID());
    fprintf(stderr, "DINPUT-PAD: X2_VIRTUAL_PAD -- a SYNTHETIC gamepad is "
                    "attached. Nothing in this run's controller behaviour came "
                    "from real hardware, and this line is here so that cannot "
                    "be mistaken.\n");
}
#endif

/*
 * Press a button or move an axis on the SYNTHETIC pad.
 *
 * Why this has to exist: SDL's virtual joystick reports zero for every axis
 * and button until something sets one. So a run with X2_VIRTUAL_PAD exercised
 * enumeration, CreateDevice, the data format and the axis ranges the game
 * asks for -- and never once exercised an actual INPUT. "Hotswap works" and
 * "the controller does nothing" were both true at the same time, and nothing
 * in the build could tell them apart.
 *
 * Names come from g_vbtn_name, the same table the SDL mapping is generated
 * from, so a name here and a button there cannot drift apart.
 *
 * Returns 0 with a reason rather than doing nothing quietly: "there is no
 * synthetic pad in this run" and "that button does not exist" are different
 * answers and the caller acts differently on each.
 */
int dinput_pad_virtual_set(const char *what, double value, double hold,
                           char *why, int whyn)
{
#ifdef X2_WITH_SDL
    static const char *const AXIS[6] = {
        "leftx", "lefty", "rightx", "righty", "lefttrigger", "righttrigger"
    };
    double now = guest_clock_now_s();
    int i;

    if (!g_virt_js) {
        snprintf(why, (size_t)whyn,
                 "this run has no synthetic pad to press (X2_VIRTUAL_PAD is "
                 "unset, or the pad attached but could not be opened). A REAL "
                 "controller is driven by the hardware, not from here.");
        return 0;
    }
    /* State of the handle we are about to press, printed once. The set
       succeeds and the read comes back up, so the question is whether this is
       still the live virtual device at all. */
    {
        static int said;
        if (!said++)
            fprintf(stderr,
                "DINPUT-PAD: press handle %p on thread %llu -- id %u "
                "(attached as %u), connected=%d, virtual=%d, %d button(s), "
                "%d axis(es)\n",
                (void *)g_virt_js,
                (unsigned long long)SDL_GetCurrentThreadID(),
                (unsigned)SDL_GetJoystickID(g_virt_js),
                (unsigned)g_virt_id, (int)SDL_JoystickConnected(g_virt_js),
                (int)SDL_IsJoystickVirtual(g_virt_id),
                SDL_GetNumJoystickButtons(g_virt_js),
                SDL_GetNumJoystickAxes(g_virt_js));
    }
    for (i = 0; i < VBTN_N; i++)
        if (!strcmp(what, g_vbtn_name[i])) {
            if (!SDL_SetJoystickVirtualButton(g_virt_js, i, true)) {
                snprintf(why, (size_t)whyn, "SDL refused button %d (%s): %s",
                         i, what, SDL_GetError());
                return 0;
            }
            g_vbtn_until[i] = now + (hold > 0.0 ? hold : 0.30);
            g_vpad_presses++;
            /*
             * READ IT BACK, through the same call the game uses.
             *
             * Setting a virtual JOYSTICK button and reading a GAMEPAD button
             * are two different layers with a mapping in between, and if that
             * mapping is not what this code thinks it is, the press is
             * accepted here and invisible there -- which is indistinguishable
             * from the game ignoring it. Reporting the read-back turns "the
             * pad does nothing" into one of two specific answers.
             */
            SDL_UpdateJoysticks();
            SDL_UpdateGamepads();
            {
                SDL_GamepadButton gb = SDL_GetGamepadButtonFromString(what);
                /* The JOYSTICK's own view, one layer below the gamepad. If
                   this is down and the gamepad is up, the mapping is the
                   problem; if this is up too, the virtual set never landed. */
                int jraw = SDL_GetJoystickButton(g_virt_js, i) ? 1 : 0;
                Pad *pp = pad_at(0);
                int back = (pp && pp->gp && gb != SDL_GAMEPAD_BUTTON_INVALID)
                           ? (SDL_GetGamepadButton(pp->gp, gb) ? 1 : 0) : -1;
                snprintf(why, (size_t)whyn,
                         "joystick button %d set (joystick itself reads %s); "
                         "gamepad \"%s\" (enum %d) now reads %s",
                         i, jraw ? "DOWN" : "UP", what, (int)gb,
                         back < 0 ? "UNREADABLE -- no open gamepad for pad 0"
                                  : (back ? "DOWN" : "UP -- SDL did NOT take "
                                            "it through the mapping"));
            }
            return 1;
        }
    for (i = 0; i < 6; i++)
        if (!strcmp(what, AXIS[i])) {
            /* -1..1 from the caller, SDL's signed 16-bit range on the wire. */
            double v = value < -1.0 ? -1.0 : (value > 1.0 ? 1.0 : value);
            short raw = (short)(v * 32767.0);
            if (!SDL_SetJoystickVirtualAxis(g_virt_js, i, raw)) {
                snprintf(why, (size_t)whyn, "SDL refused axis %d (%s): %s",
                         i, what, SDL_GetError());
                return 0;
            }
            g_vaxis_value[i] = raw;
            g_vaxis_until[i] = hold > 0.0 ? now + hold : 0.0;
            g_vpad_axis_sets++;
            return 1;
        }
    {
        size_t n = (size_t)snprintf(why, (size_t)whyn,
                                    "\"%s\" is not a button or axis on this "
                                    "pad. Buttons: ", what);
        for (i = 0; i < VBTN_N && n < (size_t)whyn; i++)
            n += (size_t)snprintf(why + n, (size_t)whyn - n, "%s ",
                                  g_vbtn_name[i]);
        if (n < (size_t)whyn)
            snprintf(why + n, (size_t)whyn - n,
                     "| axes: leftx lefty rightx righty lefttrigger "
                     "righttrigger");
    }
    return 0;
#else
    (void)what; (void)value; (void)hold;
    snprintf(why, (size_t)whyn, "built without SDL: there is no pad at all.");
    return 0;
#endif
}

/* Release whatever has been held long enough. Called once a frame beside the
   attach/detach schedule, so a press lasts real frames rather than one poll. */
static void virtual_expire(void)
{
#ifdef X2_WITH_SDL
    double now = guest_clock_now_s();
    int i;
    if (!g_virt_js) return;
    for (i = 0; i < VBTN_N; i++)
        if (g_vbtn_until[i] != 0.0 && now >= g_vbtn_until[i]) {
            g_vbtn_until[i] = 0.0;
            SDL_SetJoystickVirtualButton(g_virt_js, i, false);
        }
    for (i = 0; i < 6; i++)
        if (g_vaxis_until[i] != 0.0 && now >= g_vaxis_until[i]) {
            g_vaxis_until[i] = 0.0;
            g_vaxis_value[i] = 0;
            SDL_SetJoystickVirtualAxis(g_virt_js, i, 0);
        }
#endif
}

void dinput_pad_poll_report(void)
{
    fprintf(stderr,
        "  pad: the game read a button %lu time(s); %lu of those came back "
        "DOWN. Axes read %lu time(s), %lu off centre. SDL state refreshed "
        "%lu time(s).\n",
        g_btn_reads, g_btn_down, g_axis_reads, g_axis_offcentre, g_pad_pumps);
    if (g_btn_reads && !g_pad_pumps)
        fprintf(stderr,
            "       The game polled but SDL's pad state was NEVER refreshed, "
            "so every read returned whatever SDL last latched -- which is "
            "nothing. This is the defect dinput_pad_refresh_state exists to "
            "fix; it is not being called.\n");
    if (!g_btn_reads)
        fprintf(stderr,
            "       ZERO reads: the game is not polling the pad at all, so no "
            "press could reach it whatever the hardware does.\n");
    else if (!g_btn_down)
        fprintf(stderr,
            "       Reads happen but NONE was ever down: either nothing "
            "pressed anything, or the press is not reaching SDL's gamepad "
            "state (a virtual pad needs SDL_SetJoystickVirtual*; a real one "
            "needs its events pumped).\n");
    fprintf(stderr,
        "       %lu synthetic press(es) and %lu axis set(s) were requested "
        "over the control channel.\n", g_vpad_presses, g_vpad_axis_sets);
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
                   "Xbox 360 DirectInput pad; prompts: %s\n", i,
                   g_pad[i].name, g_pad[i].buttons,
                   g_pad[i].xbox_glyphs ? "Xbox glyphs" : "game text");
}
