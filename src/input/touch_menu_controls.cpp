#include "touch_menu_controls.h"

#include "../presentation/touch_layout.h"

#include <algorithm>
#include <array>

namespace x2::input {
namespace {

/* Above every gameplay zone id, so the overlay's elements never collide when
   the document switches between the two layouts. */
constexpr std::uint32_t kFirstMenuZone = 100;

constexpr std::array<TouchAction, kX2MenuSlotCount> kSlotActions = {
    TouchAction::MenuUp,
    TouchAction::MenuDown,
    TouchAction::MenuLeft,
    TouchAction::MenuRight,
    TouchAction::MenuA,
    TouchAction::MenuB,
    TouchAction::MenuX,
    TouchAction::MenuY,
    TouchAction::MenuLeftShoulder,
    TouchAction::MenuRightShoulder};

bool is_release(lucent::touch::Phase phase) {
  return phase == lucent::touch::Phase::ended ||
         phase == lucent::touch::Phase::canceled;
}

} // namespace

std::vector<ActionEvent> MenuControls::set_viewport(Viewport viewport) {
  auto released = cancel();
  zones_.clear();
  const X2LayoutViewport layout{
      viewport.width,           viewport.height,
      viewport.safe_area.left,  viewport.safe_area.top,
      viewport.safe_area.right, viewport.safe_area.bottom};
  std::array<X2Rect, kX2MenuSlotCount> slots{};
  std::vector<lucent::touch::Zone> router_zones;
  if (x2_layout_build_menu(layout, slots.data())) {
    for (std::uint32_t slot = 0; slot < kX2MenuSlotCount; ++slot) {
      const X2Rect &r = slots[slot];
      zones_.push_back(
          {{kFirstMenuZone + slot, r.left, r.top, r.right, r.bottom, 20},
           kSlotActions[slot],
           false});
      router_zones.push_back(zones_.back().zone);
    }
  }
  router_.set_zones(router_zones);
  return released;
}

bool MenuControls::hit(lucent::touch::Point point) const {
  return std::any_of(zones_.begin(), zones_.end(), [point](const auto &zone) {
    return point.x >= zone.zone.left && point.x < zone.zone.right &&
           point.y >= zone.zone.top && point.y < zone.zone.bottom;
  });
}

std::vector<ActionEvent>
MenuControls::route(std::span<const lucent::touch::Contact> contacts) {
  return translate(router_.route(contacts));
}

std::vector<ActionEvent> MenuControls::cancel() {
  return translate(router_.cancel());
}

std::vector<ActionEvent>
MenuControls::translate(std::span<const lucent::touch::Event> events) const {
  std::vector<ActionEvent> actions;
  actions.reserve(events.size());
  for (const auto &event : events) {
    const auto found = std::find_if(
        zones_.begin(), zones_.end(),
        [id = event.zone_id](const auto &zone) { return zone.zone.id == id; });
    if (found == zones_.end()) {
      continue;
    }
    actions.push_back({event.contact_id, event.zone_id, found->action,
                       is_release(event.phase) ? 0.0F : 1.0F, event.position,
                       event.phase});
  }
  return actions;
}

} // namespace x2::input
