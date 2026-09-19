/*
 * DOES A TOUCH ON A DRAWN CONTROL REACH THE PAD THE GAME READS?
 *
 * Every other touch owner is tested -- the source classifier, the zone layout,
 * the router, the HUD relocation -- and each of them can be entirely correct
 * while the feature does nothing, because none of them touches the pad. This
 * is the module that does: it turns an SDL finger into a zone, a zone into an
 * action, and an action into a press on the synthetic DirectInput pad, then
 * claims player one for that pad so something is actually reading it.
 *
 * That last step is not a formality. The pad only reaches the guest once a
 * player resolves to it, and on a first run on a phone neither a transient
 * assignment nor a stored reservation exists, so every touch was published
 * into a pad nobody read: the game polled buttons that were never down. The
 * overlay drew, the zones lit up, and the character did not move. A test that
 * stopped at the overlay would have passed throughout.
 *
 * So this asserts the far end. It presses where the control is actually drawn
 * -- coordinates taken from the shipping layout, never hardcoded -- and reads
 * the button back through SDL's gamepad layer, which is the same layer the
 * port's DirectInput sampling reads.
 *
 * Every check has a negative beside it: a contact outside every zone must
 * press nothing, a released control must come back up, a centred stick must
 * report no movement, and the overlay must be invisible before the game says
 * the player has control. Without those a stuck-down device or an
 * always-active overlay would pass.
 */
#include <SDL3/SDL.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <lucent/cvar.hpp>

extern "C" {
#include "dinput_pad_virtual.h"
#include "gameplay_control.h"
#include "guest_clock.h"
#include "settings.h"
#include "settings_store.h"
#include "touch_census.h"
#include "touch_runtime.h"
#include "touch_source.h"
#include "transient_controller_assignment.h"
}

#include "touch_controls.h"

/* The settings store resolves its file below the save directory. Pointing it
 * at a scratch root keeps this test off the developer's real settings, where a
 * stored touch_controls value or a stored controller reservation would decide
 * what it observes -- the reservation especially, since the player-one claim
 * below is exactly what it is checking. */
extern "C" const char *x2_save_dir(void) { return X2_TEST_TOUCH_RUNTIME_ROOT; }

namespace {

int g_failures = 0;
int g_checks = 0;

void check(bool ok, const char *what, const std::string &detail) {
  ++g_checks;
  if (ok) {
    std::printf("  ok   %s (%s)\n", what, detail.c_str());
    return;
  }
  ++g_failures;
  std::printf("  FAIL %s (%s)\n", what, detail.c_str());
}

/* The window is 1 unit of SDL's normalized finger space wide, so a contact at
 * output pixel (x, y) is dispatched at (x / width, y / height) -- the same
 * conversion x2_touch_runtime_event performs in reverse. */
void send_finger(Uint32 type, SDL_FingerID id, float x, float y, int width,
                 int height) {
  SDL_Event event{};
  event.type = type;
  event.tfinger.fingerID = id;
  event.tfinger.x = x / static_cast<float>(width);
  event.tfinger.y = y / static_cast<float>(height);
  x2_touch_runtime_event(&event);
}

std::vector<X2TouchVisual> visuals() {
  std::vector<X2TouchVisual> out(x2_touch_runtime_visuals(nullptr, 0));
  if (!out.empty())
    x2_touch_runtime_visuals(out.data(), out.size());
  return out;
}

const X2TouchVisual *find_action(const std::vector<X2TouchVisual> &all,
                                 x2::input::TouchAction action) {
  for (const auto &visual : all)
    if (visual.action == static_cast<int>(action))
      return &visual;
  return nullptr;
}

bool zone_is_active(x2::input::TouchAction action) {
  const auto all = visuals();
  const X2TouchVisual *zone = find_action(all, action);
  return zone && zone->active;
}

/* The synthetic pad is the only joystick a dummy-video run has. Opening it as
 * a gamepad is what the port's own sampling does, so a press that the mapping
 * does not carry across is a press the game would not see either. */
SDL_Gamepad *open_the_synthetic_pad() {
  int count = 0;
  SDL_JoystickID *ids = SDL_GetJoysticks(&count);
  SDL_Gamepad *pad = nullptr;
  for (int i = 0; ids && i < count; ++i) {
    if (!SDL_IsJoystickVirtual(ids[i]))
      continue;
    pad = SDL_OpenGamepad(ids[i]);
    if (pad)
      break;
  }
  SDL_free(ids);
  return pad;
}

bool button_down(SDL_Gamepad *pad, const char *name) {
  SDL_UpdateJoysticks();
  SDL_UpdateGamepads();
  const SDL_GamepadButton button = SDL_GetGamepadButtonFromString(name);
  return button != SDL_GAMEPAD_BUTTON_INVALID &&
         SDL_GetGamepadButton(pad, button);
}

float axis_value(SDL_Gamepad *pad, const char *name) {
  SDL_UpdateJoysticks();
  SDL_UpdateGamepads();
  const SDL_GamepadAxis axis = SDL_GetGamepadAxisFromString(name);
  if (axis == SDL_GAMEPAD_AXIS_INVALID)
    return 0.0F;
  return static_cast<float>(SDL_GetGamepadAxis(pad, axis)) / 32767.0F;
}

} // namespace

int main() {
  using x2::input::TouchAction;

  SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
  if (!SDL_Init(SDL_INIT_GAMEPAD | SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
    std::fprintf(stderr, "SKIP: no SDL gamepad subsystem here (%s)\n",
                 SDL_GetError());
    return 77;
  }

  const int width = 1280;
  const int height = 720;
  SDL_Window *window = SDL_CreateWindow("x2 touch runtime test", width, height,
                                        SDL_WINDOW_HIDDEN);
  if (!window) {
    std::fprintf(stderr, "SKIP: no window from this video driver (%s)\n",
                 SDL_GetError());
    return 77;
  }

  /*
   * NOTHING attaches a pad here on purpose.
   *
   * This test used to set the X2_VIRTUAL_PAD cvar and attach the synthetic
   * pad itself, which meant it proved the touch chain works GIVEN a pad --
   * and the product did not give it one. A browser run with the overlay live
   * refused all 36 of its publications with "this run has no synthetic pad to
   * press". So the pad must come from the shipping touch path or not at all.
   */
  lucent::cvar::Var<std::string> virtual_pad{"virtual_pad", ""};
  lucent::cvar::Var<std::string> virtual_pad_id{"virtual_pad_id", ""};
  lucent::cvar::register_var(virtual_pad);
  lucent::cvar::register_var(virtual_pad_id);
  check(dinput_pad_virtual_slot() < 0,
        "no synthetic pad exists before touch asks for one",
        "nothing has attached one");
  SDL_Gamepad *pad = nullptr;

  x2_settings_store_init();
  x2_settings_store()->touch_controls = X2_TOUCH_CONTROLS_ALWAYS;
  x2_touch_runtime_window(window);

  /* The census's OTHER branch, printed before anything has been touched.
     It is the one a phone or a browser tab sits in for the whole of the logo
     and the loading route, and it is printed here so the reader that parses
     it -- tests/test_web_touch_play.py -- is checked against the real text
     rather than a fixture that can drift from this file. */
  x2_touch_runtime_report("");

  /* NEGATIVE FIRST. Without it an overlay that is always visible, and a test
     that only ever looked after the heartbeat, would agree with each other. */
  check(!x2_touch_runtime_overlay_visible(),
        "the overlay is hidden before the game reports gameplay control",
        "no HUD heartbeat has arrived");

  x2_gameplay_control_hud_drawn(guest_clock_now_s());
  check(x2_touch_runtime_overlay_visible() != 0,
        "the overlay is visible once the retail HUD has drawn",
        "one heartbeat");

  const auto drawn = visuals();
  check(!drawn.empty(), "the layout publishes drawn controls",
        std::to_string(drawn.size()) + " zone(s)");

  const X2TouchVisual *jump = find_action(drawn, TouchAction::Jump);
  if (!jump) {
    std::printf("  FAIL the layout has no Jump control to press\n");
    return 1;
  }

  /* A contact nowhere near a control must do nothing. Run it before the real
     press so a pad that was somehow already down cannot be read as a pass. */
  const float empty_x = static_cast<float>(width) * 0.5F;
  const float empty_y = static_cast<float>(height) * 0.5F;
  send_finger(SDL_EVENT_FINGER_DOWN, 900, empty_x, empty_y, width, height);
  send_finger(SDL_EVENT_FINGER_UP, 900, empty_x, empty_y, width, height);

  /* The first contact routed through the shipping path is what attaches the
     pad. If the product does not attach one, nothing below can pass -- which
     is the point: this is the failure a real browser run had. */
  check(dinput_pad_virtual_slot() >= 0, "touch attached its own synthetic pad",
        "slot " + std::to_string(dinput_pad_virtual_slot()));
  pad = open_the_synthetic_pad();
  if (!pad) {
    std::fprintf(stderr,
                 "SKIP: the synthetic pad has no gamepad mapping here\n");
    return 77;
  }
  check(!button_down(pad, "y"), "a contact outside every zone presses nothing",
        "centre of the screen");

  const float jump_x = (jump->left + jump->right) * 0.5F;
  const float jump_y = (jump->top + jump->bottom) * 0.5F;
  send_finger(SDL_EVENT_FINGER_DOWN, 1, jump_x, jump_y, width, height);
  check(zone_is_active(TouchAction::Jump),
        "the drawn Jump control reports held",
        "at " + std::to_string(static_cast<int>(jump_x)) + "," +
            std::to_string(static_cast<int>(jump_y)));
  check(button_down(pad, "y"), "Jump reaches the pad the game reads",
        "gamepad button y is down");
  check(x2_transient_controller_has_assignment(0) != 0,
        "the touch pad is claimed by player one",
        "a pad no player reads is a pad the guest never polls");

  send_finger(SDL_EVENT_FINGER_UP, 1, jump_x, jump_y, width, height);
  check(!zone_is_active(TouchAction::Jump),
        "releasing clears the drawn control", "finger up");
  check(!button_down(pad, "y"), "releasing clears the pad button",
        "gamepad button y is up");

  /* The stick is the control a scroll steals first in a browser and the one a
     player uses constantly, so it gets the same treatment as a button. */
  const X2TouchVisual *stick = nullptr;
  for (const auto &visual : drawn)
    if (visual.stick) {
      stick = &visual;
      break;
    }
  if (!stick) {
    std::printf("  FAIL the layout has no movement stick\n");
    return 1;
  }
  const float centre_x = (stick->left + stick->right) * 0.5F;
  const float centre_y = (stick->top + stick->bottom) * 0.5F;
  send_finger(SDL_EVENT_FINGER_DOWN, 2, centre_x, centre_y, width, height);
  check(SDL_fabsf(axis_value(pad, "leftx")) < 0.2F &&
            SDL_fabsf(axis_value(pad, "lefty")) < 0.2F,
        "a finger resting on the stick's centre asks for no movement",
        "leftx " + std::to_string(axis_value(pad, "leftx")) + ", lefty " +
            std::to_string(axis_value(pad, "lefty")));

  const float edge_x = stick->right - (stick->right - centre_x) * 0.1F;
  send_finger(SDL_EVENT_FINGER_MOTION, 2, edge_x, centre_y, width, height);
  check(axis_value(pad, "leftx") > 0.5F,
        "dragging the stick right moves the pad's left axis right",
        "leftx " + std::to_string(axis_value(pad, "leftx")));

  send_finger(SDL_EVENT_FINGER_UP, 2, edge_x, centre_y, width, height);
  check(SDL_fabsf(axis_value(pad, "leftx")) < 0.2F,
        "lifting off the stick returns the axis to rest",
        "leftx " + std::to_string(axis_value(pad, "leftx")));

  /* THE CHORD. The documented layout reaches the four ability actions by
   * holding Powers with the left thumb and pressing an action button with the
   * right: the same zones, a different meaning in the guest. That only works
   * if both reach the pad AT ONCE. Publishing the second contact must not
   * release the first, which is the failure a single-contact implementation
   * produces and which no single-button check can see. */
  const X2TouchVisual *powers = find_action(drawn, TouchAction::Powers);
  if (!powers) {
    std::printf("  FAIL the layout has no Powers control to hold\n");
    return 1;
  }
  const float powers_x = (powers->left + powers->right) * 0.5F;
  const float powers_y = (powers->top + powers->bottom) * 0.5F;
  send_finger(SDL_EVENT_FINGER_DOWN, 4, powers_x, powers_y, width, height);
  check(axis_value(pad, "righttrigger") > 0.5F,
        "holding Powers reaches the pad",
        "righttrigger " + std::to_string(axis_value(pad, "righttrigger")));
  send_finger(SDL_EVENT_FINGER_DOWN, 5, jump_x, jump_y, width, height);
  check(
      axis_value(pad, "righttrigger") > 0.5F && button_down(pad, "y"),
      "a second thumb on an action keeps Powers held: the chord arrives whole",
      "righttrigger " + std::to_string(axis_value(pad, "righttrigger")) +
          ", y " + (button_down(pad, "y") ? "down" : "UP"));
  send_finger(SDL_EVENT_FINGER_UP, 5, jump_x, jump_y, width, height);
  check(axis_value(pad, "righttrigger") > 0.5F && !button_down(pad, "y"),
        "lifting the action thumb leaves Powers held",
        "righttrigger " + std::to_string(axis_value(pad, "righttrigger")));
  send_finger(SDL_EVENT_FINGER_UP, 4, powers_x, powers_y, width, height);
  check(axis_value(pad, "righttrigger") < 0.5F, "lifting Powers releases it",
        "righttrigger " + std::to_string(axis_value(pad, "righttrigger")));

  /* Losing the window while a control is held must not leave it held. A stuck
     button after an alt-tab or an incoming call is the failure this covers. */
  send_finger(SDL_EVENT_FINGER_DOWN, 3, jump_x, jump_y, width, height);
  check(button_down(pad, "y"), "Jump is held before focus is lost",
        "precondition for the next check");
  SDL_Event lost{};
  lost.type = SDL_EVENT_WINDOW_FOCUS_LOST;
  x2_touch_runtime_lifecycle_event(&lost);
  check(!button_down(pad, "y"), "losing the window releases what was held",
        "focus lost while Jump was down");
  check(!zone_is_active(TouchAction::Jump),
        "losing the window clears the drawn control too", "focus lost");

  /* THE INSTRUMENT. On a phone and in a browser this report is the only
   * account of the feature anyone gets, so it is asserted here against the run
   * this test just performed rather than trusted to be right. */
  X2TouchCensus census{};
  x2_touch_census_read(&census);
  const unsigned long seen = census.contacts_down + census.contacts_moved +
                             census.contacts_up + census.contacts_canceled;
  check(seen > 0, "the census counted the contacts this test sent",
        std::to_string(seen) + " contact event(s)");
  check(census.ignored_overlay_hidden == 0 && census.ignored_no_window == 0,
        "no contact was dropped before routing in this run",
        std::to_string(census.ignored_no_window) + " with no window, " +
            std::to_string(census.ignored_overlay_hidden) +
            " with the overlay hidden");
  check(census.buttons_published > 0 && census.buttons_refused == 0,
        "the census counted the presses that reached the pad",
        std::to_string(census.buttons_published) + " published, " +
            std::to_string(census.buttons_refused) + " refused");
  check(census.axes_published > 0 && census.axes_refused == 0,
        "the census counted the axis moves that reached the pad",
        std::to_string(census.axes_published) + " published, " +
            std::to_string(census.axes_refused) + " refused");
  check(census.player_one_claimed == 1,
        "the census recorded the player-one claim exactly once",
        std::to_string(census.player_one_claimed) + " claim(s), " +
            std::to_string(census.player_one_refused) + " refusal(s)");
  check(census.cancellations > 0, "the census recorded the focus-loss cancel",
        std::to_string(census.cancellations) + " cancellation(s)");

  /* A contact that arrives while the overlay is hidden must be counted as
     dropped, not lost silently -- that is the case the report exists to tell
     apart from "nothing was touched", and a run that never produces one would
     leave the distinction untested. */
  x2_settings_store()->touch_controls = X2_TOUCH_CONTROLS_OFF;
  const unsigned long dropped_before = census.ignored_overlay_hidden;
  send_finger(SDL_EVENT_FINGER_DOWN, 6, jump_x, jump_y, width, height);
  x2_touch_census_read(&census);
  check(census.ignored_overlay_hidden == dropped_before + 1,
        "a contact arriving with the overlay off is counted as dropped",
        std::to_string(census.ignored_overlay_hidden) + " dropped");
  check(!button_down(pad, "y"), "and it presses nothing", "touch_controls=OFF");
  x2_settings_store()->touch_controls = X2_TOUCH_CONTROLS_ALWAYS;

  /* Runs it for real: a report that throws or prints nothing is not an
     instrument, and nothing else in the suite calls it. */
  x2_touch_runtime_report("");

  std::printf("touch runtime: %d check(s), %d failure(s)\n", g_checks,
              g_failures);
  SDL_CloseGamepad(pad);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return g_failures ? 1 : 0;
}
