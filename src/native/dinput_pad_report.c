#include "dinput_pad_report.h"

#include "dinput_pad.h"
#include "dinput_pad_virtual.h"
#include "x2_log.h"

void dinput_pad_poll_report(void) {
  X2PadPollCounts c;
  dinput_pad_poll_counts(&c);
  x2_log_error(
      "  pad: the game read a button %lu time(s); %lu of those came back "
      "DOWN, %lu could not answer at all. Axes read %lu time(s), %lu off "
      "centre. SDL state refreshed %lu time(s).\n",
      c.button_reads, c.buttons_down, c.buttons_unreadable, c.axis_reads,
      c.axes_off_centre, c.refreshes);
  if (c.buttons_no_pad)
    x2_log_error(
        "       %lu read(s) asked a slot that holds no device at all. The "
        "game is polling a pad index this port has nothing in.\n",
        c.buttons_no_pad);
  if (c.buttons_unreadable)
    x2_log_error(
        "       %lu read(s) went to a pad SDL never handed this port a "
        "gamepad handle for. Those cannot report a press however hard it "
        "is held; a joystick probe reading DOWN beside them is the tell.\n",
        c.buttons_unreadable);
  if (c.button_reads && !c.refreshes)
    x2_log_error(
        "       The game polled but SDL's pad state was NEVER refreshed, "
        "so every read returned whatever SDL last latched -- which is "
        "nothing. This is the defect dinput_pad_refresh_state exists to "
        "fix; it is not being called.\n");
  if (!c.button_reads)
    x2_log_error(
        "       ZERO reads: the game is not polling the pad at all, so no "
        "press could reach it whatever the hardware does.\n");
  else if (!c.buttons_down)
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
        "requested; %lu press(es) were released by the expiry tick, and "
        "%lu release(s) waited for the game to read the press first.\n",
        presses, axis_sets, clears, dinput_pad_virtual_deferred_releases());
  }
}

void dinput_pad_report(void) {
  /* Once. Two endings call this -- atexit and the interrupt reports -- and
     neither covers every case, so both do it and this decides. */
  static int done;
  unsigned long opens = 0, closes = 0;
  int i, n;
  if (done++)
    return;
  n = dinput_pad_count();
  dinput_pad_device_counts(&opens, &closes);
  /* Printed at zero too, with what that means: "no pad is plugged in" and
     "this host cannot see pads" are different facts and the second one is a
     defect. */
  if (!n) {
    x2_log_info("  gamepads: NONE connected%s\n",
                opens ? " now (some were, earlier in this run)"
                      : " and none ever was -- either nothing is plugged in, "
                        "or SDL's gamepad subsystem never came up (that is "
                        "reported by name when it happens)");
    return;
  }
  x2_log_info(
      "  gamepads: %d connected (%lu connect(s), %lu disconnect(s) this "
      "run)\n",
      n, opens, closes);
  for (i = 0; i < DINPUT_PAD_MAX; i++) {
    const char *name = NULL;
    int buttons = 0, xbox_glyphs = 0;
    if (!dinput_pad_describe(i, &name, &buttons, &xbox_glyphs))
      continue;
    x2_log_info("         pad %d  \"%s\"  %d button(s), presented as an "
                "Xbox 360 DirectInput pad; prompts: %s\n",
                i, name, buttons, xbox_glyphs ? "Xbox glyphs" : "game text");
  }
}
