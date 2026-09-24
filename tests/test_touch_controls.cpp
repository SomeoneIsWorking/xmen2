#include "../src/input/touch_controls.h"

extern "C" {
#include "../src/presentation/touch_layout.h"
}

#include <algorithm>
#include <array>
#include <iostream>
#include <limits>
#include <optional>
#include <vector>

namespace {

bool power_slots() {
  using lucent::touch::Contact;
  using lucent::touch::Phase;
  using x2::input::TouchAction;
  x2::input::TouchControls controls;
  controls.set_viewport({844, 390, {32, 0, 20, 12}});
  const auto zone_of = [&controls](TouchAction action) {
    const auto zones = controls.zones();
    const auto found =
        std::find_if(zones.begin(), zones.end(), [action](const auto &zone) {
          return zone.action == action;
        });
    return found == zones.end()
               ? std::optional<x2::input::TouchControls::ZoneVisual>{}
               : std::optional{*found};
  };
  if (zone_of(TouchAction::Power1)) {
    std::cerr << "a power was drawn before the game published one\n";
    return false;
  }
  if (!controls.set_power_icons({-1, 9, -1, 2}).empty()) {
    std::cerr << "publishing powers with nothing held released something\n";
    return false;
  }
  const auto second = zone_of(TouchAction::Power2);
  const auto fourth = zone_of(TouchAction::Power4);
  if (!second || !fourth || second->power_icon != 9 ||
      fourth->power_icon != 2 || zone_of(TouchAction::Power1) ||
      zone_of(TouchAction::Power3)) {
    std::cerr << "power zones do not follow the published slots\n";
    return false;
  }
  const lucent::touch::Point at{(second->zone.left + second->zone.right) / 2,
                                (second->zone.top + second->zone.bottom) / 2};
  const auto pressed = controls.route(std::array{Contact{1, at, Phase::began}});
  if (pressed.size() != 1 || pressed.front().action != TouchAction::Power2 ||
      pressed.front().value != 1.0F) {
    std::cerr << "a drawn power does not route to its own action\n";
    return false;
  }
  if (!controls.set_power_icons({-1, 9, -1, 2}).empty()) {
    std::cerr << "republishing the same powers released a held one\n";
    return false;
  }
  const auto vanished = controls.set_power_icons({-1, -1, -1, 2});
  if (vanished.size() != 1 || vanished.front().action != TouchAction::Power2 ||
      vanished.front().value != 0.0F || zone_of(TouchAction::Power2)) {
    std::cerr << "a power that vanished under a finger stayed held\n";
    return false;
  }
  return true;
}

bool has_value(const std::vector<x2::input::ActionEvent> &events,
               x2::input::TouchAction action, float minimum) {
  return std::any_of(events.begin(), events.end(),
                     [action, minimum](const auto &event) {
                       return event.action == action && event.value >= minimum;
                     });
}

bool portrait_regions() {
  using lucent::touch::Phase;
  using x2::input::TouchAction;
  x2::input::TouchControls controls;
  controls.set_viewport({1000, 600, {20, 10, 20, 10}});
  const auto has_portrait = [&controls] {
    const auto zones = controls.zones();
    return std::any_of(zones.begin(), zones.end(), [](const auto &zone) {
      return zone.action >= TouchAction::SelectHero1;
    });
  };
  if (has_portrait()) {
    std::cerr << "portraits were inferred without a drawn rectangle\n";
    return false;
  }
  const auto base_zone_count = controls.zones().size();
  std::array<X2Rect, 4> rectangles{{{100, 100, 150, 170},
                                    {200, 100, 230, 180},
                                    {500, 150, 550, 200},
                                    {650, 100, 730, 180}}};
  controls.set_portraits(rectangles, 5);
  const auto hero3 = std::find_if(
      controls.zones().begin(), controls.zones().end(),
      [](const auto &zone) { return zone.action == TouchAction::SelectHero3; });
  if (hero3 == controls.zones().end() || hero3->zone.left != 500 ||
      hero3->zone.bottom != 200 ||
      controls.zones().size() != base_zone_count + 2) {
    std::cerr
        << "portrait regions did not preserve explicit bounds and visibility\n";
    return false;
  }
  const std::array portrait{
      lucent::touch::Contact{1, {125, 135}, Phase::began}};
  if (!has_value(controls.route(portrait), TouchAction::SelectHero1, 1)) {
    std::cerr << "published portrait rectangle did not route hero selection\n";
    return false;
  }
  if (!controls.set_portraits(rectangles, 5).empty()) {
    std::cerr << "unchanged HUD canceled an active portrait\n";
    return false;
  }
  const auto moved = controls.route(
      std::array{lucent::touch::Contact{1, {990, 590}, Phase::moved}});
  if (moved.size() != 1 || moved.front().position.x != 125 ||
      moved.front().position.y != 135) {
    std::cerr << "portrait drag left the retail selection center\n";
    return false;
  }
  const auto attack = std::find_if(
      controls.zones().begin(), controls.zones().end(),
      [](const auto &zone) { return zone.action == TouchAction::LightAttack; });
  const lucent::touch::Point attack_point{
      (attack->zone.left + attack->zone.right) / 2,
      (attack->zone.top + attack->zone.bottom) / 2};
  controls.route(
      std::array{lucent::touch::Contact{2, attack_point, Phase::began}});
  rectangles[0].right += 10;
  const auto changed = controls.set_portraits(rectangles, 5);
  if (changed.size() != 1 ||
      changed.front().action != TouchAction::SelectHero1 ||
      changed.front().phase != Phase::canceled || changed.front().value != 0 ||
      !has_value(controls.route(std::array{
                     lucent::touch::Contact{2, attack_point, Phase::moved}}),
                 TouchAction::LightAttack, 1)) {
    std::cerr
        << "changed portrait failed to cancel independently of held attack\n";
    return false;
  }
  controls.route(portrait);
  auto invalid = rectangles;
  invalid[0].right = std::numeric_limits<float>::infinity();
  const auto rejected = controls.set_portraits(invalid, 5);
  if (rejected.size() != 1 || rejected.front().phase != Phase::canceled ||
      has_portrait()) {
    std::cerr << "invalid portrait input retained stale regions or captures\n";
    return false;
  }
  for (unsigned variant = 0; variant < 4; ++variant) {
    controls.set_portraits(rectangles, 5);
    if (variant == 0)
      controls.set_portraits({}, 5);
    else if (variant == 1)
      controls.set_portraits(rectangles, 0);
    else if (variant == 2)
      controls.set_portraits(rectangles, 16);
    else
      controls.set_viewport({600, 1000, {10, 20, 10, 20}});
    if (has_portrait()) {
      std::cerr << "absent or outdated HUD regions remained touchable: "
                << variant << '\n';
      return false;
    }
  }
  return true;
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

  /* A THUMB DOES NOT LAND ON THE MIDDLE OF A CIRCLE IT CANNOT SEE.
     Every check above lands exactly on the ring's centre, which is the one
     landing that cannot tell a stick measured from the ring apart from one
     measured from the contact. This lands where a thumb actually does. */
  const float stick_radius =
      (slots[kX2SlotStick].right - slots[kX2SlotStick].left) * 0.5F;
  const lucent::touch::Point thumb{stick_centre.x + stick_radius * 0.45F,
                                   stick_centre.y + stick_radius * 0.5F};
  const auto off_centre =
      controls.route({{{3, thumb, lucent::touch::Phase::began}}});
  if (has_value(off_centre, x2::input::TouchAction::Backward, 0.01F) ||
      has_value(off_centre, x2::input::TouchAction::MoveRight, 0.01F)) {
    std::cerr << "a thumb landing off the ring's centre steered the game "
                 "before it moved\n";
    return 1;
  }
  const auto pushed = controls.route(
      {{{3, {thumb.x, thumb.y - stick_radius}, lucent::touch::Phase::moved}}});
  const auto pushed_y =
      x2::input::touch_axis_value(pushed, x2::input::TouchAction::Forward,
                                  x2::input::TouchAction::Backward);
  if (!pushed_y || *pushed_y > -0.95F) {
    std::cerr << "a full radius of travel from an off-centre landing was not "
                 "full deflection\n";
    return 1;
  }
  controls.route({{{3, thumb, lucent::touch::Phase::ended}}});

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

  /* Jump must route independently while the movement thumb remains held. */
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

  /* The HUD producer publishes exact output-pixel bounds; the input owner
     neither divides a slot into quarters nor assumes all four are visible. */
  const std::array<X2Rect, 4> portrait_rectangles{
      {{710, 30, 758, 80}, {}, {}, {}}};
  controls.set_portraits(portrait_rectangles, 1);
  const lucent::touch::Point portrait_point{734, 55};
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
  if (!portrait_regions() || !power_slots())
    return 1;
  std::cout
      << "touch controls: layout, action mapping, and cancellation passed\n";
  return 0;
}
