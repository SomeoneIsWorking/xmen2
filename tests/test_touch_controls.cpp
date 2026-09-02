#include "../src/input/touch_controls.h"

extern "C" {
#include "../src/presentation/touch_layout.h"
}

#include <algorithm>
#include <iostream>
#include <vector>

namespace {

bool has_value(const std::vector<x2::input::ActionEvent> &events,
               x2::input::TouchAction action, float minimum) {
  return std::any_of(events.begin(), events.end(),
                     [action, minimum](const auto &event) {
                       return event.action == action && event.value >= minimum;
                     });
}

} // namespace

int main() {
  x2::input::TouchControls controls;
  const X2LayoutViewport layout_viewport{1000.0F, 600.0F, 20.0F,
                                         10.0F,   20.0F,  10.0F};
  controls.set_viewport({1000.0F, 600.0F, {20.0F, 10.0F, 20.0F, 10.0F}});

  /* The probe points come from the LAYOUT, not from remembered pixels. A
     test that hardcodes where a button used to be stops testing whether the
     zone matches the drawn control the moment the layout moves -- which is
     the exact drift this shared layout exists to end. */
  X2Rect slots[kX2SlotCount];
  if (!x2_layout_build(layout_viewport, slots)) {
    std::cerr << "layout refused a viewport the controls accept\n";
    return 1;
  }
  const auto centre = [&slots](X2LayoutSlot slot) {
    return lucent::touch::Point{(slots[slot].left + slots[slot].right) * 0.5F,
                                (slots[slot].top + slots[slot].bottom) * 0.5F};
  };

  const auto stick_centre = centre(kX2SlotStick);
  const std::vector<lucent::touch::Contact> stick_down = {
      {1, stick_centre, lucent::touch::Phase::began}};
  const auto began = controls.route(stick_down);
  if (began.size() != 4 ||
      has_value(began, x2::input::TouchAction::Forward, 0.01F) ||
      has_value(began, x2::input::TouchAction::MoveLeft, 0.01F)) {
    std::cerr << "left stick did not begin with neutral directional state\n";
    return 1;
  }

  const float stick_reach =
      (slots[kX2SlotStick].right - slots[kX2SlotStick].left) * 0.4F;
  const std::vector<lucent::touch::Contact> stick_up = {
      {1,
       {stick_centre.x - stick_reach, stick_centre.y - stick_reach},
       lucent::touch::Phase::moved}};
  const auto moved = controls.route(stick_up);
  if (!has_value(moved, x2::input::TouchAction::Forward, 0.5F) ||
      !has_value(moved, x2::input::TouchAction::MoveLeft, 0.01F)) {
    std::cerr << "left stick did not produce the expected action values\n";
    return 1;
  }
  const auto left_y = x2::input::touch_axis_value(
      moved, x2::input::TouchAction::Forward, x2::input::TouchAction::Backward);
  const auto left_x =
      x2::input::touch_axis_value(moved, x2::input::TouchAction::MoveLeft,
                                  x2::input::TouchAction::MoveRight);
  if (!left_y || *left_y >= -0.5F || !left_x || *left_x >= -0.01F) {
    std::cerr << "touch controls: directional events did not compose into "
                 "signed axes\n";
    return 1;
  }

  const std::vector<lucent::touch::Contact> button = {
      {2, centre(kX2SlotLightAttack), lucent::touch::Phase::began}};
  const auto button_events = controls.route(button);
  if (!has_value(button_events, x2::input::TouchAction::LightAttack, 1.0F)) {
    std::cerr << "light-attack zone was not reachable\n";
    return 1;
  }

  /* Pause replaced the scattered utility row: a touch player with no
     controller has to be able to reach the menus, and this is the only
     button that does it. */
  const std::vector<lucent::touch::Contact> pause_button = {
      {3, centre(kX2SlotPause), lucent::touch::Phase::began}};
  const auto pause_events = controls.route(pause_button);
  if (!has_value(pause_events, x2::input::TouchAction::Pause, 1.0F)) {
    std::cerr << "pause zone was not reachable\n";
    return 1;
  }

  /* Jump belongs to the movement thumb, above the stick. */
  const std::vector<lucent::touch::Contact> jump_button = {
      {7, centre(kX2SlotJump), lucent::touch::Phase::began}};
  const auto jump_events = controls.route(jump_button);
  if (!has_value(jump_events, x2::input::TouchAction::Jump, 1.0F)) {
    std::cerr << "jump zone was not reachable\n";
    return 1;
  }

  const std::vector<lucent::touch::Contact> camera_down = {
      {4, {500.0F, 250.0F}, lucent::touch::Phase::began}};
  const auto camera_neutral = controls.route(camera_down);
  if (camera_neutral.size() != 4 ||
      has_value(camera_neutral, x2::input::TouchAction::CameraRight, 0.01F)) {
    std::cerr << "camera swipe did not capture with neutral axes\n";
    return 1;
  }
  const std::vector<lucent::touch::Contact> camera_move = {
      {4, {600.0F, 200.0F}, lucent::touch::Phase::moved}};
  const auto camera_events = controls.route(camera_move);
  if (!has_value(camera_events, x2::input::TouchAction::CameraRight, 0.5F) ||
      !has_value(camera_events, x2::input::TouchAction::CameraUp, 0.3F)) {
    std::cerr << "camera swipe was not relative to its capture point\n";
    return 1;
  }

  /* The first portrait cell, in the quarter of the party slot the HUD
     draws hero 1 into -- so the zone and the drawn face cannot disagree. */
  const X2Rect faces = slots[kX2SlotPortraits];
  const lucent::touch::Point portrait_point{
      faces.left + (faces.right - faces.left) * 0.125F,
      (faces.top + faces.bottom) * 0.5F};
  const std::vector<lucent::touch::Contact> portrait = {
      {5, portrait_point, lucent::touch::Phase::began}};
  const auto portrait_events = controls.route(portrait);
  if (portrait_events.size() != 1 ||
      portrait_events.front().action != x2::input::TouchAction::SelectHero1 ||
      portrait_events.front().position.x != portrait_point.x ||
      portrait_events.front().position.y != portrait_point.y) {
    std::cerr
        << "retail portrait zone did not preserve its pointer coordinate\n";
    return 1;
  }

  const auto zones = controls.zones();
  if (std::any_of(zones.begin(), zones.end(), [](const auto &zone) {
        return zone.visible &&
               (zone.action == x2::input::TouchAction::CameraLeft ||
                zone.action == x2::input::TouchAction::SelectHero1 ||
                zone.action == x2::input::TouchAction::SelectHero2 ||
                zone.action == x2::input::TouchAction::SelectHero3 ||
                zone.action == x2::input::TouchAction::SelectHero4);
      })) {
    std::cerr << "gesture or retail portrait hit zones leaked into the visual "
                 "overlay\n";
    return 1;
  }

  const auto canceled = controls.cancel();
  /* Not a memorised count: cancel must release EVERY control still held, and
     a second cancel must then have nothing left to release. A fixed number
     would only have to be re-typed whenever a control is added. */
  if (canceled.empty() ||
      !std::all_of(canceled.begin(), canceled.end(), [](const auto &event) {
        return event.value == 0.0F &&
               event.phase == lucent::touch::Phase::canceled;
      })) {
    std::cerr << "cancel did not release the captured controls ("
              << canceled.size() << " events)\n";
    return 1;
  }
  if (!controls.cancel().empty()) {
    std::cerr << "cancel left controls captured after releasing them\n";
    return 1;
  }

  const std::vector<lucent::touch::Contact> held_button = {
      {6, centre(kX2SlotLightAttack), lucent::touch::Phase::began}};
  controls.route(held_button);
  const auto rotated =
      controls.set_viewport({600.0F, 1000.0F, {10.0F, 20.0F, 10.0F, 20.0F}});
  if (rotated.size() != 1 ||
      rotated.front().action != x2::input::TouchAction::LightAttack ||
      rotated.front().value != 0.0F ||
      rotated.front().phase != lucent::touch::Phase::canceled) {
    std::cerr << "viewport change did not release the old layout\n";
    return 1;
  }
  std::cout
      << "touch controls: layout, action mapping, and cancellation passed\n";
  return 0;
}
