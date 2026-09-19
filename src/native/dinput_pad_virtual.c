#include "x2_log.h"
/* Synthetic gamepad lifecycle and input injection; see dinput_pad_virtual.h. */
#include "dinput_pad.h"
#include "dinput_pad_virtual.h"
#include "dinput_pad_virtual_internal.h"
#include "guest_clock.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <lucent/cvar_c.h>

#ifdef X2_WITH_SDL
#include <SDL3/SDL.h>
#endif
#ifdef X2_WITH_SDL
int g_virt_at_frame = -1; /* fN form: attach at that frame */
SDL_JoystickID g_virt_id;
SDL_Joystick *g_virt_js; /* opened so its axes/buttons can be set */
int g_virt_detach_at = -1;
char g_virtual_persistent_id[64];
unsigned long g_vbtn_clears;
unsigned long g_vpad_presses, g_vpad_axis_sets;
const char *const g_vaxis_name[6] = {"leftx",  "lefty",       "rightx",
                                     "righty", "lefttrigger", "righttrigger"};
#endif
/* The two axes SDL treats as TRIGGERS: it maps their whole signed joystick
   travel onto 0..32767, so their rest position is the axis minimum and not
   zero. Everything that writes a virtual axis has to know which it is holding.
   Index order is AXIS[] in dinput_pad_set_axis: leftx lefty rightx righty
   lefttrigger righttrigger. */
int axis_is_trigger(int i) { return i == 4 || i == 5; }
/* 0.0 (released) .. 1.0 (fully squeezed) as SDL's virtual-joystick value. */
short trigger_raw(double v) { return (short)(-32768.0 + v * 65535.0); }

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
const char *const g_vbtn_name[X2_VIRTUAL_BUTTON_COUNT] = {"a",
                                                          "b",
                                                          "x",
                                                          "y",
                                                          "back",
                                                          "start",
                                                          "leftstick",
                                                          "rightstick",
                                                          "leftshoulder",
                                                          "rightshoulder"};

/* A press held until `until` (guest seconds), so a press survives the game's
   per-frame poll the way a real thumb does. 0 = not held. */
double g_vbtn_until[X2_VIRTUAL_BUTTON_COUNT];
double g_vaxis_until[6];
short g_vaxis_value[6];

void dinput_pad_virtual_from_env(void) {
#ifdef X2_WITH_SDL
  const char *e = lucent_cvar_text("virtual_pad");

  if (!e || !*e || *e == '0')
    return;
  /* X2_VIRTUAL_PAD_ID=<id> -- the persistent id the synthetic pad reports,
   * so the PERSISTED-assignment path (a controller0 id stored in
   * x2native.conf, matched by make_identities) is exercisable at all: SDL
   * virtual joysticks carry no serial and no path, so without this every
   * synthetic pad is session-only by construction and a stored id can never
   * match. Announced for the same reason the pad itself is: an identity
   * that comes from an env var must never be mistaken for one a device
   * proved. */
  {
    const char *eid = lucent_cvar_text("virtual_pad_id");
    if (eid && *eid) {
      snprintf(g_virtual_persistent_id, sizeof g_virtual_persistent_id, "%s",
               eid);
      x2_log_error("DINPUT-PAD: X2_VIRTUAL_PAD_ID -- the synthetic "
                   "pad will report persistent id \"%s\". "
                   "SYNTHETIC: this identity comes from the "
                   "environment, not from hardware.\n",
                   eid);
    }
  }
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
    if (*end == '-')
      g_virt_detach_at = (int)strtol(end + 1, NULL, 10);
    x2_log_error("DINPUT-PAD: X2_VIRTUAL_PAD -- a SYNTHETIC gamepad will "
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
int dinput_pad_virtual_attach_for_touch(void) {
#ifdef X2_WITH_SDL
  if (g_virt_id) {
    return 1;
  }
  x2_log_error("DINPUT-PAD: attaching a SYNTHETIC gamepad because the "
               "on-screen touch controls publish through one. This pad is "
               "the touch overlay, not hardware.\n");
  virtual_attach();
  return g_virt_id != 0;
#else
  return 0;
#endif
}

void dinput_pad_virtual_tick(unsigned long frame) {
#ifdef X2_WITH_SDL
  virtual_expire();
  if (g_virt_at_frame >= 0 && frame >= (unsigned long)g_virt_at_frame) {
    g_virt_at_frame = -1;
    virtual_attach();
  }
  if (g_virt_detach_at >= 0 && frame >= (unsigned long)g_virt_detach_at) {
    g_virt_detach_at = -1;
    if (g_virt_id) {
      x2_log_error("DINPUT-PAD: X2_VIRTUAL_PAD -- UNPLUGGING the "
                   "synthetic pad at frame %lu.\n",
                   frame);
      if (g_virt_js) {
        SDL_CloseJoystick(g_virt_js);
        g_virt_js = NULL;
      }
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
                           char *why, int whyn) {
#ifdef X2_WITH_SDL
  double now = guest_clock_now_s();
  int i;

  /* Every exit from here MUST say something. This buffer is reused across
     calls, and an exit that leaves it alone reports the PREVIOUS call's
     text: asking for axis "leftx" answered "joystick button 0 set ...
     gamepad a", which is a diagnostic inventing an observation it never
     made. Stamped up front so that is impossible rather than remembered. */
  snprintf(why, (size_t)whyn,
           "(no reason recorded for \"%s\" -- a code "
           "path returned without saying anything)",
           what);

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
      x2_log_error("DINPUT-PAD: press handle %p on thread %llu -- id %u "
                   "(attached as %u), connected=%d, virtual=%d, %d button(s), "
                   "%d axis(es)\n",
                   (void *)g_virt_js,
                   (unsigned long long)SDL_GetCurrentThreadID(),
                   (unsigned)SDL_GetJoystickID(g_virt_js), (unsigned)g_virt_id,
                   (int)SDL_JoystickConnected(g_virt_js),
                   (int)SDL_IsJoystickVirtual(g_virt_id),
                   SDL_GetNumJoystickButtons(g_virt_js),
                   SDL_GetNumJoystickAxes(g_virt_js));
  }
  for (i = 0; i < X2_VIRTUAL_BUTTON_COUNT; i++)
    if (!strcmp(what, g_vbtn_name[i])) {
      if (!SDL_SetJoystickVirtualButton(g_virt_js, i, true)) {
        snprintf(why, (size_t)whyn, "SDL refused button %d (%s): %s", i, what,
                 SDL_GetError());
        return 0;
      }
      /* A negative hold is an explicit until-release state used by
         physical touch contacts. Zero retains the control-channel's
         historical short-press default. */
      g_vbtn_until[i] = hold < 0.0 ? 0.0 : now + (hold > 0.0 ? hold : 0.30);
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
        /* Is the handle we hold the one SDL is updating? Re-open the
           same id and ask again; and count what is attached, in case
           there is more than one virtual pad and the game reads the
           other. */
        {
          int njs = 0, fresh = -1;
          SDL_JoystickID *ids = SDL_GetJoysticks(&njs);
          SDL_Joystick *j2 = SDL_OpenJoystick(g_virt_id);
          if (j2) {
            fresh = SDL_GetJoystickButton(j2, i) ? 1 : 0;
            SDL_CloseJoystick(j2);
          }
          x2_log_error("DINPUT-PAD: probe -- %d joystick(s) attached; stored "
                       "handle %p reads %s, a FRESH open of id %u reads %s\n",
                       njs, (void *)g_virt_js, jraw ? "DOWN" : "UP",
                       (unsigned)g_virt_id,
                       fresh < 0 ? "UNOPENABLE" : (fresh ? "DOWN" : "UP"));
          if (ids)
            SDL_free(ids);
        }
        int back = gb != SDL_GAMEPAD_BUTTON_INVALID
                       ? dinput_pad_open_gamepad_button(0, (int)gb)
                       : -1;
        snprintf(why, (size_t)whyn,
                 "joystick button %d set (joystick itself reads %s); "
                 "gamepad \"%s\" (enum %d) now reads %s",
                 i, jraw ? "DOWN" : "UP", what, (int)gb,
                 back < 0 ? "UNREADABLE -- no open gamepad for pad 0"
                          : (back ? "DOWN"
                                  : "UP -- SDL did NOT take "
                                    "it through the mapping"));
      }
      return 1;
    }
  if (!strcmp(what, "up") || !strcmp(what, "down") || !strcmp(what, "left") ||
      !strcmp(what, "right")) {
    Uint8 hat = !strcmp(what, "up")     ? SDL_HAT_UP
                : !strcmp(what, "down") ? SDL_HAT_DOWN
                : !strcmp(what, "left") ? SDL_HAT_LEFT
                                        : SDL_HAT_RIGHT;
    if (!SDL_SetJoystickVirtualHat(g_virt_js, 0, hat)) {
      snprintf(why, (size_t)whyn, "SDL refused hat direction %s: %s", what,
               SDL_GetError());
      return 0;
    }
    SDL_UpdateJoysticks();
    SDL_UpdateGamepads();
    snprintf(why, (size_t)whyn, "virtual d-pad direction %s is down", what);
    return 1;
  }
  for (i = 0; i < 6; i++)
    if (!strcmp(what, g_vaxis_name[i])) {
      /* -1..1 from the caller, SDL's signed 16-bit range on the wire --
         EXCEPT for the triggers, which have no negative half. SDL maps a
         virtual joystick axis's whole -32768..32767 travel onto a
         trigger's 0..32767, so a trigger axis left at 0 reads as HALF
         HELD, and there is no way to say "released" on the caller's
         -1..1 scale unless one is defined. A trigger therefore takes
         0..1, and its rest position is the axis MINIMUM. */
      double v = value < -1.0 ? -1.0 : (value > 1.0 ? 1.0 : value);
      short raw;
      if (axis_is_trigger(i)) {
        if (v < 0.0)
          v = 0.0;
        raw = trigger_raw(v);
      } else {
        raw = (short)(v * 32767.0);
      }
      if (!SDL_SetJoystickVirtualAxis(g_virt_js, i, raw)) {
        snprintf(why, (size_t)whyn, "SDL refused axis %d (%s): %s", i, what,
                 SDL_GetError());
        return 0;
      }
      g_vaxis_value[i] = raw;
      g_vaxis_until[i] = hold > 0.0 ? now + hold : 0.0;
      g_vpad_axis_sets++;
      SDL_UpdateJoysticks();
      SDL_UpdateGamepads();
      {
        SDL_GamepadAxis ga = SDL_GetGamepadAxisFromString(what);
        int jraw = SDL_GetJoystickAxis(g_virt_js, i);
        int graw = (ga != SDL_GAMEPAD_AXIS_INVALID)
                       ? dinput_pad_open_gamepad_axis(0, (int)ga)
                       : 0;
        snprintf(why, (size_t)whyn,
                 "axis %d set to %d; joystick reads %d, gamepad "
                 "\"%s\" reads %d%s",
                 i, (int)raw, jraw, what, graw,
                 jraw == 0 && raw != 0 ? "  <-- the set did NOT land" : "");
      }
      return 1;
    }
  {
    size_t n = (size_t)snprintf(why, (size_t)whyn,
                                "\"%s\" is not a button or axis on this "
                                "pad. Buttons: ",
                                what);
    for (i = 0; i < X2_VIRTUAL_BUTTON_COUNT && n < (size_t)whyn; i++)
      n += (size_t)snprintf(why + n, (size_t)whyn - n, "%s ", g_vbtn_name[i]);
    if (n < (size_t)whyn)
      snprintf(why + n, (size_t)whyn - n,
               "| axes: leftx lefty rightx righty lefttrigger "
               "righttrigger");
  }
  return 0;
#else
  (void)what;
  (void)value;
  (void)hold;
  snprintf(why, (size_t)whyn, "built without SDL: there is no pad at all.");
  return 0;
#endif
}

int dinput_pad_virtual_slot(void) {
#ifdef X2_WITH_SDL
  if (!g_virt_id)
    return -1;
  return dinput_pad_for_joystick_id((unsigned int)g_virt_id);
#else
  return -1;
#endif
}

const char *dinput_pad_virtual_identity_override(unsigned int joystick_id) {
#ifdef X2_WITH_SDL
  if (g_virt_id && (unsigned int)g_virt_id == joystick_id &&
      g_virtual_persistent_id[0])
    return g_virtual_persistent_id;
#else
  (void)joystick_id;
#endif
  return NULL;
}

void dinput_pad_virtual_counts(unsigned long *presses, unsigned long *axis_sets,
                               unsigned long *clears) {
  if (presses)
    *presses = g_vpad_presses;
  if (axis_sets)
    *axis_sets = g_vpad_axis_sets;
  if (clears)
    *clears = g_vbtn_clears;
}
