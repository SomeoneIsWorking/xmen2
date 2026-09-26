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

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <lucent/cvar.hpp>

extern "C" {
#include "dinput_pad.h"
#include "dinput_pad_virtual.h"
#include "directinput_controller_sample.h"
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

/*
 * Poll the way the GAME polls, before asking SDL what it holds.
 *
 * The pad keeps a press until the guest has read it -- a finger down and up
 * inside one pump is otherwise invisible. A test that reads SDL directly is
 * not the reader that release is waiting for, so without this the pad is
 * right and the assertion is wrong. Defined below, next to the other
 * guest-facing helpers.
 */
void sample_as_the_guest_does();

bool button_down(SDL_Gamepad *pad, const char *name) {
  sample_as_the_guest_does();
  SDL_UpdateJoysticks();
  SDL_UpdateGamepads();
  const SDL_GamepadButton button = SDL_GetGamepadButtonFromString(name);
  return button != SDL_GAMEPAD_BUTTON_INVALID &&
         SDL_GetGamepadButton(pad, button);
}

/*
 * What the GUEST reads.
 *
 * Everything above this line stops at SDL. The game does not read SDL: it
 * reads a DIJOYSTATE2 buffer that x2_directinput_controller_write fills from
 * a capture of the pad in the DirectInput inventory. A press that reaches the
 * SDL gamepad and not this buffer is a press the game never sees, and the two
 * are separated by the DirectInput button order -- the exact place a mapping
 * lands every press one button off.
 */
constexpr int32_t kAxisLo = -32768;
constexpr int32_t kAxisHi = 32767;
/* DIJOYSTATE2 puts the button array at +48, one byte each, 0x80 for down.
   BTN[] in dinput_pad.c fixes the order: 3 is Y, which is what Jump
   publishes. */
constexpr uint32_t kButtonsOffset = 48;
constexpr int kDirectInputButtonY = 3;
constexpr int kDirectInputButtonA = 0;

int guest_buttons(int slot) {
  SDL_UpdateJoysticks();
  SDL_UpdateGamepads();
  X2DirectInputControllerSample sample;
  if (!x2_directinput_controller_capture(slot, kAxisLo, kAxisHi, &sample)) {
    return -1; /* No device at that slot at all -- distinct from "none down". */
  }
  return sample.buttons;
}

/* The byte at the guest's own button offset, or -1 when the slot has no
   device. Written through the shipping serializer, not reconstructed here. */
/* One guest-facing axis in the range the game asks for, or the range's
   midpoint when the slot has no device -- which is what dinput_pad_axis
   itself returns for a missing pad, so a caller cannot read "no device" as
   "hard left". The callers below check the device separately. */
int32_t guest_axis(int slot, int axis) {
  SDL_UpdateJoysticks();
  SDL_UpdateGamepads();
  X2DirectInputControllerSample sample;
  if (!x2_directinput_controller_capture(slot, kAxisLo, kAxisHi, &sample)) {
    return 0;
  }
  return sample.axes[axis];
}

int guest_button_byte(int slot, int button) {
  SDL_UpdateJoysticks();
  SDL_UpdateGamepads();
  X2DirectInputControllerSample sample;
  if (!x2_directinput_controller_capture(slot, kAxisLo, kAxisHi, &sample)) {
    return -1;
  }
  unsigned char state[64];
  std::memset(state, 0, sizeof state);
  x2_directinput_controller_write(&sample, state, sizeof state);
  return state[kButtonsOffset + static_cast<unsigned>(button)];
}

void sample_as_the_guest_does() {
  X2DirectInputControllerSample sample;
  /* dinput_joystick_state latches SDL once and reads the whole state out of
     that latch; a sampler that skipped the latch would be waiting on a
     counter the real guest moves and this test never does. */
  dinput_pad_refresh_state();
  x2_directinput_controller_capture(dinput_pad_virtual_slot(), kAxisLo, kAxisHi,
                                    &sample);
}

float axis_value(SDL_Gamepad *pad, const char *name) {
  sample_as_the_guest_does();
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
  /*
   * THE WHOLE RUN BELOW HAPPENS UNDER A STALE CONTROLLER RESERVATION.
   *
   * This is the ordinary state of a phone whose owner once paired a
   * Bluetooth pad: the stored assignment survives, the device does not. It
   * used to stop touch claiming player one -- while x2_player_input_sync,
   * which resolves the reservation through dinput_pad_for_persistent_id,
   * left player one unassigned because no such device is here. Player one
   * ended up with no controller at all and touch declined to fill the
   * vacancy it exists to fill.
   *
   * Setting it here rather than in a case of its own means every check that
   * follows -- the claim, the presses, the axes, the guest's own buffer --
   * is made in its presence.
   */
  check(x2_settings_assign_controller(x2_settings_store(),
                                      "a-pad-that-is-not-here", 0) != 0,
        "a stale reservation for an absent controller is stored",
        "the ordinary state of a phone that once saw a Bluetooth pad");
  check(x2_settings_player_controller(x2_settings_store(), 0) != nullptr,
        "and player one's stored reservation names it",
        "so the run below is made under one");
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
        "the touch pad is claimed by player one despite the stale reservation",
        "the reserved controller is not here, so it holds nothing");

  /* Past SDL, into the buffer the game actually reads. */
  const int slot = dinput_pad_virtual_slot();
  check(guest_buttons(slot) == (1 << kDirectInputButtonY),
        "Jump arrives at the guest as DirectInput button 3 and nothing else",
        "buttons bitmap " + std::to_string(guest_buttons(slot)));
  check(guest_button_byte(slot, kDirectInputButtonY) == 0x80,
        "the guest's own button byte reads pressed",
        "DIJOYSTATE2+" + std::to_string(kButtonsOffset + kDirectInputButtonY));

  send_finger(SDL_EVENT_FINGER_UP, 1, jump_x, jump_y, width, height);
  check(!zone_is_active(TouchAction::Jump),
        "releasing clears the drawn control", "finger up");
  check(!button_down(pad, "y"), "releasing clears the pad button",
        "gamepad button y is up");
  check(guest_buttons(slot) == 0, "and the guest sees no button held",
        "buttons bitmap " + std::to_string(guest_buttons(slot)));
  check(guest_button_byte(slot, kDirectInputButtonY) == 0,
        "the guest's own button byte reads released", "finger up");

  /*
   * A tap the game had no chance to see. This is the browser's ordinary case,
   * not an edge: a finger down and the same finger up arrive in one pump, and
   * the guest polls between pumps, so without a deferred release every press
   * is published and invisible. Nothing is read between these two calls on
   * purpose -- reading is what the release is waiting for.
   */
  send_finger(SDL_EVENT_FINGER_DOWN, 9, jump_x, jump_y, width, height);
  send_finger(SDL_EVENT_FINGER_UP, 9, jump_x, jump_y, width, height);
  check(guest_buttons(slot) == (1 << kDirectInputButtonY),
        "a press and release inside one pump still reaches the guest",
        "buttons bitmap " + std::to_string(guest_buttons(slot)));
  dinput_pad_virtual_tick(0);
  check(guest_buttons(slot) == 0, "and it lets go once the guest has read it",
        "buttons bitmap " + std::to_string(guest_buttons(slot)));

  /*
   * And a DIAGNOSTIC read must not satisfy that wait. The probe that prints
   * what is held reads the same pad; when its reads counted, they made the
   * press look already seen and it was dropped a millisecond after it was
   * published -- 167,890 guest reads in a browser run, not one of them DOWN.
   */
  send_finger(SDL_EVENT_FINGER_DOWN, 11, jump_x, jump_y, width, height);
  (void)dinput_pad_button_uncounted(slot, kDirectInputButtonY);
  send_finger(SDL_EVENT_FINGER_UP, 11, jump_x, jump_y, width, height);
  check(guest_buttons(slot) == (1 << kDirectInputButtonY),
        "a probe reading the pad does not count as the guest having read it",
        "buttons bitmap " + std::to_string(guest_buttons(slot)));
  /* The reader-side view has to be able to say both things: a browser run
     where it never speaks would be indistinguishable from one where it never
     looked. */
  check(dinput_pad_virtual_report_reader_view() == kDirectInputButtonY,
        "the reader view finds the held button", "while it is held");
  dinput_pad_virtual_tick(0);
  check(dinput_pad_virtual_report_reader_view() == -1,
        "and finds nothing once it is let go", "after the release");

  /* And another button's reader does not count as this one's. The game reads
     all ten buttons out of one latch, so a total moves nine times over for
     values nobody asked about; keyed on a total, this press was released on
     the read of a button the player never touched. */
  send_finger(SDL_EVENT_FINGER_DOWN, 13, jump_x, jump_y, width, height);
  (void)dinput_pad_button(slot, kDirectInputButtonA);
  send_finger(SDL_EVENT_FINGER_UP, 13, jump_x, jump_y, width, height);
  check(guest_buttons(slot) == (1 << kDirectInputButtonY),
        "a read of a different button is not this button's reader",
        "buttons bitmap " + std::to_string(guest_buttons(slot)));
  dinput_pad_virtual_tick(0);

  /* The stick is the control a scroll steals first in a browser and the one a
     player uses constantly, so it gets the same treatment as a button. */
  const X2TouchVisual *stick = nullptr;
  for (const auto &visual : drawn)
    if (visual.kind == X2_TOUCH_VISUAL_STICK) {
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
  check(guest_axis(slot, DINPUT_PAD_AXIS_X) > kAxisHi / 2,
        "the guest's own X axis reads right",
        "DIJOYSTATE2 lX " +
            std::to_string(guest_axis(slot, DINPUT_PAD_AXIS_X)));
  check(SDL_abs(guest_axis(slot, DINPUT_PAD_AXIS_Y)) < kAxisHi / 4,
        "and its Y axis is not dragged along with it",
        "DIJOYSTATE2 lY " +
            std::to_string(guest_axis(slot, DINPUT_PAD_AXIS_Y)));

  send_finger(SDL_EVENT_FINGER_UP, 2, edge_x, centre_y, width, height);
  check(SDL_fabsf(axis_value(pad, "leftx")) < 0.2F,
        "lifting off the stick returns the axis to rest",
        "leftx " + std::to_string(axis_value(pad, "leftx")));
  check(SDL_abs(guest_axis(slot, DINPUT_PAD_AXIS_X)) < kAxisHi / 4,
        "and the guest's own X axis returns to centre",
        "DIJOYSTATE2 lX " +
            std::to_string(guest_axis(slot, DINPUT_PAD_AXIS_X)));

  /* THE POWERS. The hero's RT powers are drawn only where the game has one,
   * and each is the chord the retail ring teaches -- RT with its slot's face
   * button -- arriving whole. A button shared with another held control stays
   * down until the last holder lets go, and a power that vanishes under a
   * finger is released rather than left held. */
  const auto centre = [](const X2TouchVisual &zone) {
    return std::pair{(zone.left + zone.right) * 0.5F,
                     (zone.top + zone.bottom) * 0.5F};
  };
  check(!find_action(visuals(), TouchAction::Power1),
        "no power is drawn before the game has published any", "fresh run");
  const int powers[X2_POWER_SLOTS] = {4, -1, 7, -1};
  x2_touch_runtime_power_slots(powers);
  const auto with_powers = visuals();
  const X2TouchVisual *power1 = find_action(with_powers, TouchAction::Power1);
  const X2TouchVisual *power3 = find_action(with_powers, TouchAction::Power3);
  check(power1 && power3 && power1->power_icon == 4 && power3->power_icon == 7,
        "each published power is drawn with its own atlas cell",
        power1 && power3 ? std::to_string(power1->power_icon) + ", " +
                               std::to_string(power3->power_icon)
                         : "missing");
  check(!find_action(with_powers, TouchAction::Power2) &&
            !find_action(with_powers, TouchAction::Power4),
        "a slot with no power has no button", "slots 2 and 4 empty");
  if (!power1 || !power3) {
    return 1;
  }
  const auto [p1_x, p1_y] = centre(*power1);
  const auto [p3_x, p3_y] = centre(*power3);
  send_finger(SDL_EVENT_FINGER_DOWN, 5, jump_x, jump_y, width, height);
  send_finger(SDL_EVENT_FINGER_DOWN, 4, p3_x, p3_y, width, height);
  check(axis_value(pad, "righttrigger") > 0.5F && button_down(pad, "x") &&
            button_down(pad, "y"),
        "the third power is RT with X, beside a held Jump",
        "righttrigger " + std::to_string(axis_value(pad, "righttrigger")) +
            ", x " + (button_down(pad, "x") ? "down" : "UP"));
  send_finger(SDL_EVENT_FINGER_DOWN, 6, p1_x, p1_y, width, height);
  check(button_down(pad, "a"), "the first power adds A to the held RT",
        "a " + std::string(button_down(pad, "a") ? "down" : "UP"));
  send_finger(SDL_EVENT_FINGER_UP, 4, p3_x, p3_y, width, height);
  check(axis_value(pad, "righttrigger") > 0.5F && !button_down(pad, "x"),
        "lifting one power leaves RT down for the other still held",
        "righttrigger " + std::to_string(axis_value(pad, "righttrigger")));
  const int none[X2_POWER_SLOTS] = {-1, -1, -1, -1};
  x2_touch_runtime_power_slots(none);
  check(axis_value(pad, "righttrigger") < 0.5F && !button_down(pad, "a"),
        "a power that vanishes under a finger is released",
        "righttrigger " + std::to_string(axis_value(pad, "righttrigger")) +
            ", a " + (button_down(pad, "a") ? "down" : "UP"));
  check(!find_action(visuals(), TouchAction::Power1), "and is no longer drawn",
        "slots cleared");
  send_finger(SDL_EVENT_FINGER_UP, 6, p1_x, p1_y, width, height);
  send_finger(SDL_EVENT_FINGER_UP, 5, jump_x, jump_y, width, height);

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
  check(census.ignored_no_window == 0,
        "no contact was dropped before routing in this run",
        std::to_string(census.ignored_no_window) + " with no window");
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
  check(census.cancelled_window_gone > 0,
        "the census recorded the focus-loss cancel under that cause",
        std::to_string(census.cancelled_window_gone) + " for a lost window, " +
            std::to_string(census.cancelled_source_changed) +
            " for a changed source");

  /*
   * A CONTACT WITH NO DRAWN CONTROL UNDER IT IS THE RETAIL POINTER.
   *
   * This is the case the player meets first and the one nothing covered: the
   * intro and the main menu draw no overlay, so every finger there was
   * counted and discarded and a phone could not leave the title screen. The
   * check is that the contact becomes a pointer press at its own position and
   * presses no pad button, because those screens are the retail GUI.
   */
  x2_settings_store()->touch_controls = X2_TOUCH_CONTROLS_OFF;
  const unsigned long pointer_before = census.pointer_events;
  X2TouchPointer pointer{};
  while (x2_touch_runtime_take_pointer(&pointer)) {
  }
  send_finger(SDL_EVENT_FINGER_DOWN, 6, jump_x, jump_y, width, height);
  x2_touch_census_read(&census);
  check(census.pointer_events == pointer_before + 1,
        "a contact with no control drawn becomes the retail pointer",
        std::to_string(census.pointer_events) + " pointer event(s)");
  check(x2_touch_runtime_take_pointer(&pointer) && pointer.button_change == 1,
        "and it is published as a pointer PRESS",
        "button_change " + std::to_string(pointer.button_change));
  check(std::abs(pointer.x - jump_x) < 1.0F &&
            std::abs(pointer.y - jump_y) < 1.0F,
        "at the position the finger was at, not a control's centre",
        std::to_string(pointer.x) + "," + std::to_string(pointer.y) +
            " against " + std::to_string(jump_x) + "," +
            std::to_string(jump_y));
  check(!button_down(pad, "y"),
        "and it presses no pad button: that screen is the retail GUI",
        "touch_controls=OFF");

  /* A second finger must not take retail's one mouse button off the first. */
  const unsigned long refused_before = census.pointer_refused;
  send_finger(SDL_EVENT_FINGER_DOWN, 7, jump_x + 4.0F, jump_y, width, height);
  x2_touch_census_read(&census);
  check(census.pointer_refused == refused_before + 1,
        "a second finger does not take the one mouse button off the first",
        std::to_string(census.pointer_refused) + " refused");

  send_finger(SDL_EVENT_FINGER_UP, 6, jump_x, jump_y, width, height);
  bool released = false;
  while (x2_touch_runtime_take_pointer(&pointer)) {
    released = released || pointer.button_change == 0;
  }
  check(released, "lifting the owning finger releases the button",
        "a press with no release would leave retail's button down");
  x2_settings_store()->touch_controls = X2_TOUCH_CONTROLS_ALWAYS;

  /* THE MENU PAD. Off gameplay the overlay draws the controller the retail
     menus are navigated with. Both classes of finger: one that begins on a
     pad button presses that button, one that begins anywhere else is the
     retail pointer's and presses nothing on the pad. */
  x2_gameplay_control_reset();
  check(!x2_touch_runtime_overlay_visible() &&
            x2_touch_runtime_has_visuals() != 0,
        "off gameplay the menu pad is drawn instead of the controls",
        "no HUD heartbeat");
  {
    const auto menu = visuals();
    const X2TouchVisual *menu_a = find_action(menu, TouchAction::MenuA);
    check(menu_a != nullptr && !find_action(menu, TouchAction::Jump),
          "the menu pad draws A and none of the gameplay controls",
          std::to_string(menu.size()) + " visual(s)");
    if (!menu_a) {
      return 1;
    }
    const float a_x = (menu_a->left + menu_a->right) * 0.5F;
    const float a_y = (menu_a->top + menu_a->bottom) * 0.5F;
    X2TouchPointer drained{};
    while (x2_touch_runtime_take_pointer(&drained)) {
    }

    send_finger(SDL_EVENT_FINGER_DOWN, 20, a_x, a_y, width, height);
    check(zone_is_active(TouchAction::MenuA) && button_down(pad, "a"),
          "a finger on the menu pad's A presses the pad's A",
          "gamepad button a is down");
    check(guest_buttons(slot) == (1 << kDirectInputButtonA),
          "and the guest reads DirectInput button 0 and nothing else",
          "buttons bitmap " + std::to_string(guest_buttons(slot)));
    X2TouchPointer none{};
    check(!x2_touch_runtime_take_pointer(&none),
          "a menu pad press is not also a click on the retail GUI",
          "no pointer event");
    send_finger(SDL_EVENT_FINGER_UP, 20, a_x, a_y, width, height);
    check(!button_down(pad, "a"), "lifting it releases A", "button a is up");

    send_finger(SDL_EVENT_FINGER_DOWN, 21, empty_x, empty_y, width, height);
    X2TouchPointer click{};
    const bool clicked = x2_touch_runtime_take_pointer(&click);
    check(clicked && click.button_change == 1 && !button_down(pad, "a"),
          "a finger off the pad is the retail pointer and presses no button",
          "centre of the screen");
    /* Dragged over A, it stays the pointer's. */
    send_finger(SDL_EVENT_FINGER_MOTION, 21, a_x, a_y, width, height);
    check(!button_down(pad, "a"), "and dragging it onto A does not press A",
          "the contact began off the pad");
    send_finger(SDL_EVENT_FINGER_UP, 21, a_x, a_y, width, height);
    while (x2_touch_runtime_take_pointer(&drained)) {
    }

    /* A held pad button is let go when gameplay takes the screen, even if
       the finger never moves again. */
    send_finger(SDL_EVENT_FINGER_DOWN, 22, a_x, a_y, width, height);
    check(button_down(pad, "a"), "A is held again", "before gameplay");
    x2_gameplay_control_hud_drawn(guest_clock_now_s());
    (void)x2_touch_runtime_take_pointer(&drained);
    check(!button_down(pad, "a"),
          "gameplay taking the screen lets go of a held menu button",
          "no finger event arrived");
    send_finger(SDL_EVENT_FINGER_UP, 22, a_x, a_y, width, height);
  }

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
