#include "x2_log.h"
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
 * The host reports no DIDOI_FFACTUATOR flag because it has no force feedback.
 *
 * WHAT A PAD LOOKS LIKE. It is presented as the DirectInput layout of an Xbox
 * 360 pad, a layout the game's controller-type enumeration already names.
 * Left stick is X/Y,
 * right stick on Rx/Ry, both triggers COMBINED on Z (left positive, right
 * negative -- the 360's actual DirectInput behaviour, not a simplification),
 * d-pad on POV 0, and ten buttons in the 360's order.
 */
#include "dinput_pad.h"
#include "dinput_pad_identity.h"
#include "dinput_pad_virtual.h"
#include "guest_clock.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef X2_WITH_SDL
#include <SDL3/SDL.h>
#endif

typedef struct {
  int used;
#ifdef X2_WITH_SDL
  SDL_Gamepad *gp;
  SDL_JoystickID id;
#endif
  unsigned char inst[16]; /* unique live-run DirectInput identity */
  unsigned char prod[16]; /* product GUID -- the PIDVID form */
  char name[128];
  char persistent_id[64];
  int buttons;
  int xbox_glyphs;
} Pad;

static Pad g_pad[DINPUT_PAD_MAX];
static int g_scanned;
static unsigned long g_opens, g_closes;
static uint64_t g_generation;
#ifdef X2_WITH_SDL

static int slot_of_id(SDL_JoystickID id) {
  int i;
  for (i = 0; i < DINPUT_PAD_MAX; i++)
    if (g_pad[i].used && g_pad[i].id == id)
      return i;
  return -1;
}

static uint64_t identity_hash(const char *tag, const char *value,
                              const SDL_GUID *guid, unsigned ordinal) {
  const unsigned char *s;
  uint64_t h = UINT64_C(1469598103934665603);
  size_t i;

#define HASH_BYTES(ptr, count)                                                 \
  do {                                                                         \
    const unsigned char *hb_ = (const unsigned char *)(ptr);                   \
    size_t hn_;                                                                \
    for (hn_ = 0; hn_ < (count); hn_++) {                                      \
      h ^= hb_[hn_];                                                           \
      h *= UINT64_C(1099511628211);                                            \
    }                                                                          \
  } while (0)
  for (s = (const unsigned char *)tag; *s; s++)
    HASH_BYTES(s, 1);
  if (value)
    for (s = (const unsigned char *)value; *s; s++)
      HASH_BYTES(s, 1);
  HASH_BYTES(guid->data, sizeof guid->data);
  for (i = 0; i < sizeof ordinal; i++)
    HASH_BYTES((unsigned char *)&ordinal + i, 1);
#undef HASH_BYTES
  return h;
}

static void make_identities(Pad *p, SDL_Gamepad *gp, SDL_JoystickID id,
                            const SDL_GUID *guid, unsigned ordinal) {
  const char *serial = SDL_GetGamepadSerial(gp);
  const char *path = SDL_GetGamepadPath(gp);
  const char *tag;
  const char *value;
  uint64_t stable;
  uint32_t live = (uint32_t)id;
  int i;

  /* The announced synthetic identity wins, so a run can drive the same
     persisted-assignment resolution a stored controller0 id goes through. */
  {
    const char *override = dinput_pad_virtual_identity_override(id);
    if (override) {
      snprintf(p->persistent_id, sizeof p->persistent_id, "%s", override);
      return;
    }
  }
  /* SDL's GUID is a PRODUCT identity, despite its name. Preserve its device
     description but mix in SDL's unique live instance id. For equal GUIDs,
     different joystick ids now produce different DirectInput instances. */
  memcpy(p->inst, guid->data, sizeof p->inst);
  for (i = 0; i < 4; i++) {
    uint32_t word;
    memcpy(&word, p->inst + i * 4, sizeof word);
    word ^= live * (UINT32_C(0x9e3779b9) + (uint32_t)i * UINT32_C(0x85ebca6b));
    memcpy(p->inst + i * 4, &word, sizeof word);
  }

  if (serial && serial[0]) {
    tag = "serial";
    value = serial;
    ordinal = 0;
  } else if (path && path[0]) {
    tag = "path";
    value = path;
    ordinal = 0;
  } else {
    /* A session id distinguishes units but is not a physical identity. */
    tag = "session";
    value = p->name;
    ordinal = (unsigned)id;
  }
  stable = identity_hash(tag, value, guid, ordinal);
  snprintf(p->persistent_id, sizeof p->persistent_id,
           strcmp(tag, "session") ? "sdl-%04x-%04x-%016llx"
                                  : "sdl-session-%04x-%04x-%016llx",
           SDL_GetGamepadVendorForID(id), SDL_GetGamepadProductForID(id),
           (unsigned long long)stable);
}

static void pad_open(SDL_JoystickID id) {
  int i;
  SDL_Gamepad *gp;
  Pad *p;
  SDL_GUID gu;
  const char *nm;

  if (slot_of_id(id) >= 0)
    return;
  for (i = 0; i < DINPUT_PAD_MAX; i++)
    if (!g_pad[i].used)
      break;
  if (i == DINPUT_PAD_MAX) {
    x2_log_error("DINPUT-PAD: a %d-th pad appeared and there is no slot "
                 "for it. The game supports four players; the limit here "
                 "is DINPUT_PAD_MAX in dinput_pad.h.\n",
                 DINPUT_PAD_MAX + 1);
    return;
  }
  if (!(gp = SDL_OpenGamepad(id))) {
    x2_log_error("DINPUT-PAD: SDL_OpenGamepad(%u) failed (%s); this pad "
                 "is NOT enumerated rather than enumerated and dead.\n",
                 (unsigned)id, SDL_GetError());
    return;
  }
  p = &g_pad[i];
  memset(p, 0, sizeof *p);
  p->used = 1;
  p->gp = gp;
  p->id = id;
  gu = SDL_GetJoystickGUIDForID(id);
  dinput_pad_make_product_guid(p->prod, SDL_GetGamepadVendorForID(id),
                               SDL_GetGamepadProductForID(id));
  nm = SDL_GetGamepadNameForID(id);
  snprintf(p->name, sizeof p->name, "%s", nm ? nm : "Gamepad");
  make_identities(p, gp, id, &gu, (unsigned)i);
  /* The DirectInput button order is the fixed 360 mapping below. */
  p->buttons = 10;
  p->xbox_glyphs =
      dinput_pad_type_uses_xbox_glyphs((int)SDL_GetGamepadType(gp));
  g_opens++;
  g_generation++;
  x2_log_error("DINPUT-PAD: pad %d connected -- \"%s\" (vendor 0x%04x "
               "product 0x%04x). Presented to the game as an Xbox 360 "
               "DirectInput pad: 6 axes, 10 buttons, 1 POV.\n",
               i, p->name, SDL_GetGamepadVendorForID(id),
               SDL_GetGamepadProductForID(id));
}

static void pad_close(SDL_JoystickID id) {
  int i = slot_of_id(id);
  if (i < 0)
    return;
  SDL_CloseGamepad(g_pad[i].gp);
  x2_log_error("DINPUT-PAD: pad %d disconnected -- \"%s\".\n", i,
               g_pad[i].name);
  memset(&g_pad[i], 0, sizeof g_pad[i]);
  g_closes++;
  g_generation++;
}
#endif /* X2_WITH_SDL */

uint64_t dinput_pad_generation(void) { return g_generation; }

void dinput_pad_refresh(void) {
#ifdef X2_WITH_SDL
  int n = 0, i, j;
  SDL_JoystickID *ids;

  if (!SDL_WasInit(SDL_INIT_GAMEPAD)) {
    /*
     * SDL DROPS JOYSTICK BUTTONS WHEN NOTHING HAS KEYBOARD FOCUS, and it
     * drops them in a way that is almost impossible to see: axis state is
     * still written through, so the pad enumerates, its axes move, and
     * every button reads released forever. Measured here as 71,700 button
     * polls with 0 down while a press was held across thousands of them.
     *
     * That is the wrong policy for THIS program whatever the run looks
     * like. The window belongs to the guest -- a 2005 game creating it
     * through a Win32 layer that SDL only backs -- so SDL's notion of
     * which window holds focus is not something the port controls, and a
     * headless run has a hidden window that can never take focus at all.
     * Gating input on it means input silently disappearing.
     *
     * Set BEFORE the subsystem starts: the hint is read at init.
     */
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
      /* Said ONCE, and it matters: with no gamepad subsystem every
         enumeration below reports zero pads, which is indistinguishable
         from a machine with no pad plugged in. */
      static int told;
      if (!told++)
        x2_log_error("DINPUT-PAD: SDL_INIT_GAMEPAD failed (%s). NO "
                     "pad can be enumerated in this run -- that is "
                     "the subsystem missing, not an empty USB "
                     "port.\n",
                     SDL_GetError());
      return;
    }
  }
  if (!(ids = SDL_GetGamepads(&n)))
    return;
  for (i = 0; i < n; i++)
    pad_open(ids[i]);
  /* And close anything SDL no longer lists. Done by rescan rather than by
     event so that this is correct however it is called -- an event-only path
     misses every pad that was already gone when the first poll happened. */
  for (i = 0; i < DINPUT_PAD_MAX; i++) {
    if (!g_pad[i].used)
      continue;
    for (j = 0; j < n; j++)
      if (ids[j] == g_pad[i].id)
        break;
    if (j == n)
      pad_close(g_pad[i].id);
  }
  SDL_free(ids);
  g_scanned = 1;
#endif
}

int dinput_pad_count(void) {
  int i, n = 0;
  if (!g_scanned)
    dinput_pad_refresh();
  for (i = 0; i < DINPUT_PAD_MAX; i++)
    if (g_pad[i].used)
      n++;
  return n;
}

static Pad *pad_at(int pad) {
  if (pad < 0 || pad >= DINPUT_PAD_MAX || !g_pad[pad].used)
    return NULL;
  return &g_pad[pad];
}

int dinput_pad_instance_guid(int pad, unsigned char guid[16]) {
  Pad *p = pad_at(pad);
  if (!p)
    return 0;
  memcpy(guid, p->inst, 16);
  return 1;
}

int dinput_pad_product_guid(int pad, unsigned char guid[16]) {
  Pad *p = pad_at(pad);
  if (!p)
    return 0;
  memcpy(guid, p->prod, 16);
  return 1;
}

const char *dinput_pad_name(int pad) {
  Pad *p = pad_at(pad);
  return p ? p->name : NULL;
}

const char *dinput_pad_persistent_id(int pad) {
  Pad *p = pad_at(pad);
  return p ? p->persistent_id : NULL;
}
int dinput_pad_persistent_id_is_stable(int pad) {
  Pad *p = pad_at(pad);
  return p && strncmp(p->persistent_id, "sdl-session-", 12) != 0;
}
int dinput_pad_for_persistent_id(const char *id) {
  int i;
  if (!id || !id[0])
    return -1;
  for (i = 0; i < DINPUT_PAD_MAX; i++)
    if (g_pad[i].used && dinput_pad_persistent_id_is_stable(i) &&
        strcmp(g_pad[i].persistent_id, id) == 0)
      return i;
  return -1;
}
int dinput_pad_for_guid(const unsigned char guid[16]) {
  int i;
  for (i = 0; i < DINPUT_PAD_MAX; i++)
    if (g_pad[i].used && memcmp(g_pad[i].inst, guid, 16) == 0)
      return i;
  return -1;
}

int dinput_pad_for_joystick_id(unsigned int joystick_id) {
#ifdef X2_WITH_SDL
  return slot_of_id((SDL_JoystickID)joystick_id);
#else
  (void)joystick_id;
  return -1;
#endif
}

uint32_t dinput_pad_device_id(int pad) {
#ifdef X2_WITH_SDL
  Pad *p = pad_at(pad);
  return p ? (uint32_t)p->id : 0;
#else
  (void)pad;
  return 0;
#endif
}

int dinput_pad_button_count(int pad) {
  Pad *p = pad_at(pad);
  return p ? p->buttons : 0;
}

int dinput_pad_uses_xbox_glyphs(int pad) {
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
static unsigned long g_pad_pumps, g_vbtn_clears;

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
void dinput_pad_refresh_state(void) {
#ifdef X2_WITH_SDL
  g_pad_pumps++;
  SDL_UpdateGamepads();
#endif
}

int dinput_pad_button(int pad, int button) {
#ifdef X2_WITH_SDL
  Pad *p = pad_at(pad);
  int down;
  g_btn_reads++;
  if (!p || button < 0 || button >= 10)
    return 0;
  down = SDL_GetGamepadButton(p->gp, BTN[button]) ? 1 : 0;
  if (down)
    g_btn_down++;
  return down;
#else
  (void)pad;
  (void)button;
  return 0;
#endif
}

float dinput_pad_trigger_pressure(int pad, int trigger) {
#ifdef X2_WITH_SDL
  Pad *p = pad_at(pad);
  int raw;
  SDL_GamepadAxis axis;
  if (!p || (trigger != 0 && trigger != 1))
    return 0.0f;
  axis = trigger == 0 ? SDL_GAMEPAD_AXIS_LEFT_TRIGGER
                      : SDL_GAMEPAD_AXIS_RIGHT_TRIGGER;
  raw = SDL_GetGamepadAxis(p->gp, axis);
  return raw > 0 ? (float)raw / 32767.0f : 0.0f;
#else
  (void)pad;
  (void)trigger;
  return 0.0f;
#endif
}

int32_t dinput_pad_axis(int pad, int axis, int32_t lo, int32_t hi) {
  g_axis_reads++;
  /* Centred is the MIDPOINT of the range the game set, not zero: the game
     asked for [-1000, 1000] and would read a hard-left stick if a host that
     had been given [0, 65535] returned 0. */
  int32_t mid = lo + (hi - lo) / 2;
#ifdef X2_WITH_SDL
  Pad *p = pad_at(pad);
  int raw = 0;
  if (!p)
    return mid;
  switch (axis) {
  case DINPUT_PAD_AXIS_X:
    raw = SDL_GetGamepadAxis(p->gp, SDL_GAMEPAD_AXIS_LEFTX);
    break;
  case DINPUT_PAD_AXIS_Y:
    raw = SDL_GetGamepadAxis(p->gp, SDL_GAMEPAD_AXIS_LEFTY);
    break;
  case DINPUT_PAD_AXIS_RX:
    raw = SDL_GetGamepadAxis(p->gp, SDL_GAMEPAD_AXIS_RIGHTX);
    break;
  case DINPUT_PAD_AXIS_RY:
    raw = SDL_GetGamepadAxis(p->gp, SDL_GAMEPAD_AXIS_RIGHTY);
    break;
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
    /* ONE trigger held alone reaches the axis EXTREME, exactly as a fully
       deflected stick does -- the two triggers divide the axis between
       them by opposing each other, not by each owning half of it. This
       read `(l - r) / 2`, which put a fully squeezed trigger at half
       scale; the game applies one DIPROP_RANGE to every axis, so a
       binding on Z- then resolved to 0.5 where a stick binding resolved to
       1.0. The old test could not see it: it checked the SIGN. */
    raw = l - r; /* 0..32767 each -> -32767..32767 */
    break;
  }
  case DINPUT_PAD_AXIS_RZ:
    raw = 0;
    break;
  default:
    return mid;
  }
  /* SDL's -32768..32767 into the caller's range, with the midpoint exact. */
  if (raw < -32767)
    raw = -32767;
  if (raw)
    g_axis_offcentre++;
  return mid + (int32_t)((int64_t)raw * (hi - lo) / 2 / 32767);
#else
  (void)pad;
  (void)axis;
  return mid;
#endif
}

int dinput_pad_open_gamepad_button(int pad, int gamepad_button) {
#ifdef X2_WITH_SDL
  Pad *p = pad_at(pad);
  if (!p || !p->gp || gamepad_button < 0 ||
      gamepad_button >= SDL_GAMEPAD_BUTTON_COUNT)
    return -1;
  return SDL_GetGamepadButton(p->gp, (SDL_GamepadButton)gamepad_button) ? 1 : 0;
#else
  (void)pad;
  (void)gamepad_button;
  return -1;
#endif
}

int dinput_pad_open_gamepad_axis(int pad, int gamepad_axis) {
#ifdef X2_WITH_SDL
  Pad *p = pad_at(pad);
  if (!p || !p->gp || gamepad_axis < 0 ||
      gamepad_axis >= SDL_GAMEPAD_AXIS_COUNT)
    return 0;
  return SDL_GetGamepadAxis(p->gp, (SDL_GamepadAxis)gamepad_axis);
#else
  (void)pad;
  (void)gamepad_axis;
  return 0;
#endif
}

uint32_t dinput_pad_pov(int pad) {
#ifdef X2_WITH_SDL
  Pad *p = pad_at(pad);
  int up, down, left, right;
  if (!p)
    return 0xFFFFFFFFu;
  up = SDL_GetGamepadButton(p->gp, SDL_GAMEPAD_BUTTON_DPAD_UP);
  down = SDL_GetGamepadButton(p->gp, SDL_GAMEPAD_BUTTON_DPAD_DOWN);
  left = SDL_GetGamepadButton(p->gp, SDL_GAMEPAD_BUTTON_DPAD_LEFT);
  right = SDL_GetGamepadButton(p->gp, SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
  /* Hundredths of a degree clockwise from north, and CENTRED is
     0xFFFFFFFF -- not 0, which is north. A host that returned 0 for centred
     would hold "up" down for the whole run. */
  if (up && !left && !right)
    return 0;
  if (up && right)
    return 4500;
  if (right && !up && !down)
    return 9000;
  if (down && right)
    return 13500;
  if (down && !left && !right)
    return 18000;
  if (down && left)
    return 22500;
  if (left && !up && !down)
    return 27000;
  if (up && left)
    return 31500;
  return 0xFFFFFFFFu;
#else
  (void)pad;
  return 0xFFFFFFFFu;
#endif
}

void dinput_pad_poll_report(void) {
  x2_log_error(
      "  pad: the game read a button %lu time(s); %lu of those came back "
      "DOWN. Axes read %lu time(s), %lu off centre. SDL state refreshed "
      "%lu time(s).\n",
      g_btn_reads, g_btn_down, g_axis_reads, g_axis_offcentre, g_pad_pumps);
  if (g_btn_reads && !g_pad_pumps)
    x2_log_error(
        "       The game polled but SDL's pad state was NEVER refreshed, "
        "so every read returned whatever SDL last latched -- which is "
        "nothing. This is the defect dinput_pad_refresh_state exists to "
        "fix; it is not being called.\n");
  if (!g_btn_reads)
    x2_log_error(
        "       ZERO reads: the game is not polling the pad at all, so no "
        "press could reach it whatever the hardware does.\n");
  else if (!g_btn_down)
    x2_log_error(
        "       Reads happen but NONE was ever down: either nothing "
        "pressed anything, or the press is not reaching SDL's gamepad "
        "state (a virtual pad needs SDL_SetJoystickVirtual*; a real one "
        "needs its events pumped).\n");
  {
    unsigned long presses, axis_sets, clears;
    dinput_pad_virtual_counts(&presses, &axis_sets, &clears);
    x2_log_error(
        "       %lu synthetic press(es) and %lu axis set(s) were "
        "requested over the control channel; %lu press(es) were released "
        "by the expiry tick.\n",
        presses, axis_sets, clears);
  }
}

void dinput_pad_report(void) {
  /* Once. Two endings call this -- atexit and the interrupt reports -- and
     neither covers every case, so both do it and this decides. */
  static int done;
  int i, n;
  if (done++)
    return;
  n = dinput_pad_count();
  /* Printed at zero too, with what that means: "no pad is plugged in" and
     "this host cannot see pads" are different facts and the second one is a
     defect. */
  if (!n) {
    x2_log_info("  gamepads: NONE connected%s\n",
                g_opens ? " now (some were, earlier in this run)"
                        : " and none ever was -- either nothing is plugged in, "
                          "or SDL's gamepad subsystem never came up (that is "
                          "reported by name when it happens)");
    return;
  }
  x2_log_info(
      "  gamepads: %d connected (%lu connect(s), %lu disconnect(s) this "
      "run)\n",
      n, g_opens, g_closes);
  for (i = 0; i < DINPUT_PAD_MAX; i++)
    if (g_pad[i].used)
      x2_log_info("         pad %d  \"%s\"  %d button(s), presented as an "
                  "Xbox 360 DirectInput pad; prompts: %s\n",
                  i, g_pad[i].name, g_pad[i].buttons,
                  g_pad[i].xbox_glyphs ? "Xbox glyphs" : "game text");
}
