#include "dinput_pad.h"

#include "dinput_pad_internal.h"
#include "dinput_pad_report.h"
#include "dinput_pad_virtual.h"
#include "pad_stick_dead_zone.h"

#include <math.h>
#include <stdint.h>

/* The six DirectInput axes this pad presents; see dinput_pad.h. */
#define X2_DIRECTINPUT_SAMPLE_AXES 6

static int read_button(int pad, int button, int counted);
static int32_t read_axis(int pad, int axis, int32_t lo, int32_t hi,
                         int counted);

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
/* Reads that could not have answered DOWN whatever was pressed: no pad in
   that slot, or a pad SDL never gave a gamepad handle for. Counted because
   SDL_GetGamepadButton(NULL, ...) is false, which is the same answer as a
   button nobody touched. */
static unsigned long g_btn_unreadable;
/* Reads aimed at a slot that holds no device at all. Separate from the one
   above because they are different defects: no pad there, versus a pad SDL
   never gave us a handle for. Both return the same false. */
static unsigned long g_btn_no_pad;
/* Per BUTTON and per AXIS, because "the game read a button" is not "the game
   read THIS button": one poll reads all ten buttons and six axes out of a
   single latch, so a total moves fifteen times over for values nobody asked
   about. A press waiting to be seen has to wait for its own reader. */
static unsigned long g_btn_reads_by_index[10];
static unsigned long g_axis_reads_by_index[X2_DIRECTINPUT_SAMPLE_AXES];

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
void dinput_pad_poll_counts(struct X2PadPollCounts *out) {
  if (!out) {
    return;
  }
  out->button_reads = g_btn_reads;
  out->buttons_down = g_btn_down;
  out->buttons_unreadable = g_btn_unreadable;
  out->buttons_no_pad = g_btn_no_pad;
  out->axis_reads = g_axis_reads;
  out->axes_off_centre = g_axis_offcentre;
  out->refreshes = g_pad_pumps;
}

void dinput_pad_refresh_state(void) {
#ifdef X2_WITH_SDL
  g_pad_pumps++;
  SDL_UpdateGamepads();
  (void)dinput_pad_virtual_report_reader_view();
#endif
}

unsigned long dinput_pad_button_read_count(int button) {
  if (button < 0 || button >= 10) {
    return 0;
  }
  return g_btn_reads_by_index[button];
}

unsigned long dinput_pad_axis_read_count(int axis) {
  if (axis < 0 || axis >= X2_DIRECTINPUT_SAMPLE_AXES) {
    return 0;
  }
  return g_axis_reads_by_index[axis];
}

int dinput_pad_button(int pad, int button) {
  g_btn_reads++;
  if (button >= 0 && button < 10) {
    g_btn_reads_by_index[button]++;
  }
  return read_button(pad, button, 1);
}

int dinput_pad_button_uncounted(int pad, int button) {
  return read_button(pad, button, 0);
}

static int read_button(int pad, int button, int counted) {
#ifdef X2_WITH_SDL
  SDL_Gamepad *gp = NULL;
  X2PadSlotState slot = dinput_pad_handle(pad, &gp);
  int down;
  if (slot == X2_PAD_SLOT_EMPTY) {
    if (counted) {
      g_btn_no_pad++;
    }
    return 0;
  }
  if (button < 0 || button >= 10)
    return 0;
  if (slot == X2_PAD_SLOT_NO_HANDLE) {
    if (counted) {
      g_btn_unreadable++;
    }
    return 0;
  }
  down = SDL_GetGamepadButton(gp, BTN[button]) ? 1 : 0;
  if (down && counted)
    g_btn_down++;
  return down;
#else
  (void)pad;
  (void)button;
  (void)counted;
  return 0;
#endif
}

float dinput_pad_trigger_pressure(int pad, int trigger) {
#ifdef X2_WITH_SDL
  SDL_Gamepad *gp = NULL;
  int raw;
  SDL_GamepadAxis axis;
  if (dinput_pad_handle(pad, &gp) != X2_PAD_SLOT_READY ||
      (trigger != 0 && trigger != 1))
    return 0.0f;
  axis = trigger == 0 ? SDL_GAMEPAD_AXIS_LEFT_TRIGGER
                      : SDL_GAMEPAD_AXIS_RIGHT_TRIGGER;
  raw = SDL_GetGamepadAxis(gp, axis);
  return raw > 0 ? (float)raw / 32767.0f : 0.0f;
#else
  (void)pad;
  (void)trigger;
  return 0.0f;
#endif
}

int32_t dinput_pad_axis(int pad, int axis, int32_t lo, int32_t hi) {
  g_axis_reads++;
  if (axis >= 0 && axis < X2_DIRECTINPUT_SAMPLE_AXES) {
    g_axis_reads_by_index[axis]++;
  }
  return read_axis(pad, axis, lo, hi, 1);
}

int32_t dinput_pad_axis_uncounted(int pad, int axis, int32_t lo, int32_t hi) {
  return read_axis(pad, axis, lo, hi, 0);
}

#ifdef X2_WITH_SDL
/*
 * One component of a stick, after the stick's radial dead zone.
 *
 * The dead zone needs BOTH components, which is why a stick is read here as a
 * pair and never axis by axis. The synthetic pad is exempt: the touch stick
 * that drives it measures its own travel and has its own dead zone
 * (src/input/thumb_stick.cpp), and a second one would swallow the start of
 * every gentle push.
 */
static int read_stick_axis(int pad, SDL_Gamepad *gp, int axis) {
  const int left = axis == DINPUT_PAD_AXIS_X || axis == DINPUT_PAD_AXIS_Y;
  const int vertical = axis == DINPUT_PAD_AXIS_Y || axis == DINPUT_PAD_AXIS_RY;
  const float x =
      (float)SDL_GetGamepadAxis(gp, left ? SDL_GAMEPAD_AXIS_LEFTX
                                         : SDL_GAMEPAD_AXIS_RIGHTX) /
      32767.0f;
  const float y =
      (float)SDL_GetGamepadAxis(gp, left ? SDL_GAMEPAD_AXIS_LEFTY
                                         : SDL_GAMEPAD_AXIS_RIGHTY) /
      32767.0f;
  X2PadStick stick = {x, y};
  if (pad != dinput_pad_virtual_slot()) {
    stick = x2_pad_stick_dead_zone(x, y,
                                   left ? X2_PAD_LEFT_STICK_DEAD_ZONE
                                        : X2_PAD_RIGHT_STICK_DEAD_ZONE);
  }
  return (int)lroundf((vertical ? stick.y : stick.x) * 32767.0f);
}
#endif

static int32_t read_axis(int pad, int axis, int32_t lo, int32_t hi,
                         int counted) {
  /* Centred is the MIDPOINT of the range the game set, not zero: the game
     asked for [-1000, 1000] and would read a hard-left stick if a host that
     had been given [0, 65535] returned 0. */
  int32_t mid = lo + (hi - lo) / 2;
#ifdef X2_WITH_SDL
  SDL_Gamepad *gp = NULL;
  int raw = 0;
  if (dinput_pad_handle(pad, &gp) != X2_PAD_SLOT_READY)
    return mid;
  switch (axis) {
  case DINPUT_PAD_AXIS_X:
  case DINPUT_PAD_AXIS_Y:
  case DINPUT_PAD_AXIS_RX:
  case DINPUT_PAD_AXIS_RY:
    raw = read_stick_axis(pad, gp, axis);
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
    int l = SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
    int r = SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
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
  if (raw && counted)
    g_axis_offcentre++;
  return mid + (int32_t)((int64_t)raw * (hi - lo) / 2 / 32767);
#else
  (void)pad;
  (void)axis;
  (void)counted;
  return mid;
#endif
}

int dinput_pad_open_gamepad_button(int pad, int gamepad_button) {
#ifdef X2_WITH_SDL
  SDL_Gamepad *gp = NULL;
  if (dinput_pad_handle(pad, &gp) != X2_PAD_SLOT_READY || gamepad_button < 0 ||
      gamepad_button >= SDL_GAMEPAD_BUTTON_COUNT)
    return -1;
  return SDL_GetGamepadButton(gp, (SDL_GamepadButton)gamepad_button) ? 1 : 0;
#else
  (void)pad;
  (void)gamepad_button;
  return -1;
#endif
}

int dinput_pad_open_gamepad_axis(int pad, int gamepad_axis) {
#ifdef X2_WITH_SDL
  SDL_Gamepad *gp = NULL;
  if (dinput_pad_handle(pad, &gp) != X2_PAD_SLOT_READY || gamepad_axis < 0 ||
      gamepad_axis >= SDL_GAMEPAD_AXIS_COUNT)
    return 0;
  return SDL_GetGamepadAxis(gp, (SDL_GamepadAxis)gamepad_axis);
#else
  (void)pad;
  (void)gamepad_axis;
  return 0;
#endif
}

uint32_t dinput_pad_pov(int pad) {
#ifdef X2_WITH_SDL
  SDL_Gamepad *gp = NULL;
  int up, down, left, right;
  if (dinput_pad_handle(pad, &gp) != X2_PAD_SLOT_READY)
    return 0xFFFFFFFFu;
  up = SDL_GetGamepadButton(gp, SDL_GAMEPAD_BUTTON_DPAD_UP);
  down = SDL_GetGamepadButton(gp, SDL_GAMEPAD_BUTTON_DPAD_DOWN);
  left = SDL_GetGamepadButton(gp, SDL_GAMEPAD_BUTTON_DPAD_LEFT);
  right = SDL_GetGamepadButton(gp, SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
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
