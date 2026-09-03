#include "touch_controls.h"

#include "../presentation/touch_layout.h"

#include <algorithm>
#include <array>

namespace x2::input {
namespace {

constexpr std::uint32_t left_stick = 1;
constexpr std::uint32_t camera_swipe = 2;

float clamp_unit(float value) { return std::clamp(value, 0.0F, 1.0F); }

float clamp_axis(float value) { return std::clamp(value, -1.0F, 1.0F); }

void add_button_events(std::vector<ActionEvent> &out,
                       const lucent::touch::Event &event, TouchAction action) {
  const float value = event.phase == lucent::touch::Phase::ended ||
                              event.phase == lucent::touch::Phase::canceled
                          ? 0.0F
                          : 1.0F;
  out.push_back({event.contact_id, event.zone_id, action, value, event.position,
                 event.phase});
}

void add_stick_events(std::vector<ActionEvent> &out,
                      const lucent::touch::Event &event,
                      const lucent::touch::Zone &zone,
                      const std::array<TouchAction, 4> &actions) {
  const float center_x = (zone.left + zone.right) * 0.5F;
  const float center_y = (zone.top + zone.bottom) * 0.5F;
  const float radius_x = (zone.right - zone.left) * 0.5F;
  const float radius_y = (zone.bottom - zone.top) * 0.5F;
  const float horizontal = clamp_axis((event.position.x - center_x) / radius_x);
  const float vertical = clamp_axis((event.position.y - center_y) / radius_y);
  const bool release = event.phase == lucent::touch::Phase::ended ||
                       event.phase == lucent::touch::Phase::canceled;
  const std::array<float, 4> values =
      release
          ? std::array<float, 4>{0.0F, 0.0F, 0.0F, 0.0F}
          : std::array<float, 4>{clamp_unit(-vertical), clamp_unit(vertical),
                                 clamp_unit(-horizontal),
                                 clamp_unit(horizontal)};
  for (std::size_t index = 0; index < actions.size(); ++index)
    out.push_back({event.contact_id, event.zone_id, actions[index],
                   values[index], event.position, event.phase});
}

void add_camera_events(std::vector<ActionEvent> &out,
                       const lucent::touch::Event &event,
                       const lucent::touch::Zone &zone) {
  const float travel =
      std::min(zone.right - zone.left, zone.bottom - zone.top) * 0.22F;
  const bool release = event.phase == lucent::touch::Phase::ended ||
                       event.phase == lucent::touch::Phase::canceled;
  const float horizontal =
      release || travel <= 0.0F
          ? 0.0F
          : clamp_axis((event.position.x - event.origin.x) / travel);
  const float vertical =
      release || travel <= 0.0F
          ? 0.0F
          : clamp_axis((event.position.y - event.origin.y) / travel);
  const std::array<TouchAction, 4> actions = {
      TouchAction::CameraUp, TouchAction::CameraDown, TouchAction::CameraLeft,
      TouchAction::CameraRight};
  const std::array<float, 4> values = {
      clamp_unit(-vertical), clamp_unit(vertical), clamp_unit(-horizontal),
      clamp_unit(horizontal)};
  for (std::size_t index = 0; index < actions.size(); ++index)
    out.push_back({event.contact_id, event.zone_id, actions[index],
                   values[index], event.position, event.phase});
}

} // namespace

std::optional<float> touch_axis_value(std::span<const ActionEvent> events,
                                      TouchAction negative,
                                      TouchAction positive) {
  std::optional<float> negative_value;
  std::optional<float> positive_value;
  for (const auto &event : events) {
    if (event.action == negative)
      negative_value = event.value;
    else if (event.action == positive)
      positive_value = event.value;
  }
  if (!negative_value && !positive_value)
    return std::nullopt;
  return clamp_axis(positive_value.value_or(0.0F) -
                    negative_value.value_or(0.0F));
}

std::vector<ActionEvent> TouchControls::set_viewport(Viewport viewport) {
  auto released = cancel();
  viewport_ = viewport;
  rebuild_zones();
  return released;
}

void TouchControls::rebuild_zones() {
  zones_.clear();
  X2LayoutViewport layout_viewport{
      viewport_.width,           viewport_.height,
      viewport_.safe_area.left,  viewport_.safe_area.top,
      viewport_.safe_area.right, viewport_.safe_area.bottom};
  X2Rect slots[kX2SlotCount];
  if (!x2_layout_build(layout_viewport, slots)) {
    // No usable area: no zones. Distinct from "zones that cover nothing" --
    // the router is told there is nothing to route against.
    const std::vector<lucent::touch::Zone> empty;
    router_.set_zones(empty);
    return;
  }

  auto add = [this](std::uint32_t id, X2Rect r, int priority,
                    TouchAction action, bool stick, bool visible = true) {
    zones_.push_back({{id, r.left, r.top, r.right, r.bottom, priority},
                      action,
                      stick,
                      visible});
  };

  // Movement and the action cluster come STRAIGHT from the layout. Their
  // rectangles are not recomputed here, because the previous version's
  // eighteen hand-tuned fractions were what let the drawn HUD and the
  // touchable zones drift apart.
  add(left_stick, slots[kX2SlotStick], 0, TouchAction::MoveLeft, true);
  add(10, slots[kX2SlotLightAttack], 20, TouchAction::LightAttack, false);
  add(11, slots[kX2SlotHeavyAttack], 20, TouchAction::HeavyAttack, false);
  add(12, slots[kX2SlotUse], 20, TouchAction::Use, false);
  add(20, slots[kX2SlotPowers], 20, TouchAction::Powers, false);
  add(13, slots[kX2SlotJump], 20, TouchAction::Jump, false);
  add(40, slots[kX2SlotPause], 20, TouchAction::Pause, false);

  // Camera is an invisible relative swipe over the playfield -- everything
  // the controls and the HUD do not claim. Lowest priority, so a combat
  // chord or a portrait tap never moves it.
  {
    const float left = layout_viewport.safe_left;
    const float top = slots[kX2SlotVitals].bottom;
    const float right = layout_viewport.width - layout_viewport.safe_right;
    const float bottom = slots[kX2SlotStick].top;
    if (bottom > top && right > left)
      add(camera_swipe, {left, top, right, bottom}, -10,
          TouchAction::CameraLeft, false, false);
  }

  // The party portraits are WHERE THE HUD DRAWS THEM, because both read the
  // same slot. These zones emit pointer events so the retail click handler,
  // including direct hero selection, remains the only behavior owner --
  // which is also why they are invisible: the game draws them itself.
  {
    const X2Rect faces = slots[kX2SlotPortraits];
    const float cell = (faces.right - faces.left) * 0.25F;
    const TouchAction heroes[4] = {
        TouchAction::SelectHero1, TouchAction::SelectHero2,
        TouchAction::SelectHero3, TouchAction::SelectHero4};
    for (std::uint32_t i = 0; i < 4; ++i)
      add(50 + i,
          {faces.left + cell * static_cast<float>(i), faces.top,
           faces.left + cell * static_cast<float>(i + 1), faces.bottom},
          30, heroes[i], false, false);
  }

  std::vector<lucent::touch::Zone> router_zones;
  router_zones.reserve(zones_.size());
  for (const auto &zone : zones_)
    router_zones.push_back(zone.zone);
  router_.set_zones(router_zones);
}

std::vector<ActionEvent>
TouchControls::route(std::span<const lucent::touch::Contact> contacts) {
  const auto events = router_.route(contacts);
  return translate(events);
}

std::vector<ActionEvent> TouchControls::cancel() {
  const auto events = router_.cancel();
  return translate(events);
}

std::vector<ActionEvent>
TouchControls::translate(std::span<const lucent::touch::Event> events) const {
  std::vector<ActionEvent> actions;
  actions.reserve(events.size() * 4);
  for (const auto &event : events) {
    const auto found = std::find_if(
        zones_.begin(), zones_.end(),
        [id = event.zone_id](const auto &zone) { return zone.zone.id == id; });
    if (found == zones_.end())
      continue;
    if (found->stick) {
      if (event.zone_id == left_stick) {
        add_stick_events(actions, event, found->zone,
                         {TouchAction::Forward, TouchAction::Backward,
                          TouchAction::MoveLeft, TouchAction::MoveRight});
      }
    } else if (event.zone_id == camera_swipe) {
      add_camera_events(actions, event, found->zone);
    } else {
      add_button_events(actions, event, found->action);
    }
  }
  return actions;
}

} // namespace x2::input
