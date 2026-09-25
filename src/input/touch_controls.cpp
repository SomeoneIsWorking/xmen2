#include "touch_controls.h"

#include "../presentation/touch_layout.h"
#include "thumb_stick.h"

#include <algorithm>
#include <array>
#include <cmath>

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
                      const lucent::touch::Event &event, ThumbStick &stick,
                      const X2Rect &ring,
                      const std::array<TouchAction, 4> &actions) {
  stick.set_travel(std::min(ring.right - ring.left, ring.bottom - ring.top) *
                   0.5F);
  const ThumbStick::Deflection deflection = stick.track(event);
  const std::array<float, 4> values = {
      clamp_unit(-deflection.y), clamp_unit(deflection.y),
      clamp_unit(-deflection.x), clamp_unit(deflection.x)};
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
  hud_ = {};
  rebuild_zones();
  return released;
}

bool PointerOwner::accepts(std::int64_t contact_id,
                           lucent::touch::Phase phase) {
  if (!contact_ && phase == lucent::touch::Phase::began)
    contact_ = contact_id;
  if (contact_ != contact_id)
    return false;
  if (phase == lucent::touch::Phase::ended ||
      phase == lucent::touch::Phase::canceled)
    contact_.reset();
  return true;
}

std::vector<ActionEvent>
PortraitPointer::route(std::span<const ActionEvent> events) {
  std::vector<ActionEvent> selected;
  for (const auto &event : events) {
    if (!clicks_retail_pointer(event.action))
      continue;
    if (owner_.accepts(event.contact_id, event.phase))
      selected.push_back(event);
  }
  return selected;
}

namespace {

bool same_rects(std::span<const X2Rect> a, std::span<const X2Rect> b) {
  return std::equal(a.begin(), a.end(), b.begin(), b.end(),
                    [](const X2Rect &x, const X2Rect &y) {
                      return x.left == y.left && x.top == y.top &&
                             x.right == y.right && x.bottom == y.bottom;
                    });
}

} // namespace

/* The drawn HUD regions for a mask, all-or-nothing: one rectangle that is not
   a real on-screen area drops the whole group rather than routing to it.
   Returns the accepted mask. */
unsigned TouchControls::accept_regions(std::span<const X2Rect> regions,
                                       unsigned mask,
                                       std::span<X2Rect> out) const {
  std::fill(out.begin(), out.end(), X2Rect{});
  if (regions.size() != out.size() || (mask & ~((1u << out.size()) - 1u)))
    return 0;
  for (unsigned i = 0; i < out.size(); ++i) {
    if (!(mask & (1u << i)))
      continue;
    const auto &rect = regions[i];
    if (!std::isfinite(rect.left) || !std::isfinite(rect.top) ||
        !std::isfinite(rect.right) || !std::isfinite(rect.bottom) ||
        rect.left < 0 || rect.top < 0 || rect.right > viewport_.width ||
        rect.bottom > viewport_.height || rect.right <= rect.left ||
        rect.bottom <= rect.top) {
      std::fill(out.begin(), out.end(), X2Rect{});
      return 0;
    }
    out[i] = rect;
  }
  return mask;
}

std::vector<ActionEvent> TouchControls::set_hud(const X2HudRegions &regions) {
  X2HudRegions next{};
  next.portrait_mask =
      accept_regions(regions.portraits, regions.portrait_mask, next.portraits);
  next.potion_mask =
      accept_regions(regions.potions, regions.potion_mask, next.potions);
  next.menu_icon_mask = accept_regions(regions.menu_icons,
                                       regions.menu_icon_mask, next.menu_icons);
  if (same_rects(next.portraits, hud_.portraits) &&
      same_rects(next.potions, hud_.potions) &&
      same_rects(next.menu_icons, hud_.menu_icons) &&
      next.portrait_mask == hud_.portrait_mask &&
      next.potion_mask == hud_.potion_mask &&
      next.menu_icon_mask == hud_.menu_icon_mask)
    return {};
  auto released = translate(hud_router_.cancel());
  hud_ = next;
  rebuild_zones();
  return released;
}

std::vector<ActionEvent>
TouchControls::set_power_icons(const std::array<int, 4> &icons) {
  if (icons == power_icons_)
    return {};
  auto released = translate(router_.cancel());
  power_icons_ = icons;
  rebuild_zones();
  return released;
}

X2Rect TouchControls::port_menu_rect(X2Rect waiting) const {
  constexpr unsigned all_icons = (1u << X2_HUD_MENU_ICONS) - 1u;
  if (hud_.menu_icon_mask != all_icons)
    return waiting;
  // One more step along the row the game spaced its own icons on, at their
  // size, kept on screen.
  const X2Rect &last = hud_.menu_icons[X2_HUD_MENU_ICONS - 1];
  const float step = last.left - hud_.menu_icons[X2_HUD_MENU_ICONS - 2].left;
  const X2Rect next{last.left + step, last.top, last.right + step, last.bottom};
  if (step <= 0.0F || next.right > viewport_.width - viewport_.safe_area.right)
    return waiting;
  return next;
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
    hud_router_.set_zones(empty);
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
  // The stick captures the whole lower-left reach, not only its ring; the
  // ring is drawn where the thumb lands.
  stick_ring_ = slots[kX2SlotStick];
  add(left_stick, x2_layout_stick_reach(layout_viewport, slots), 0,
      TouchAction::MoveLeft, true);
  add(10, slots[kX2SlotLightAttack], 20, TouchAction::LightAttack, false);
  add(11, slots[kX2SlotHeavyAttack], 20, TouchAction::HeavyAttack, false);
  add(12, slots[kX2SlotUse], 20, TouchAction::Use, false);
  add(13, slots[kX2SlotJump], 20, TouchAction::Jump, false);
  for (std::uint32_t i = 0; i < power_icons_.size(); ++i) {
    if (power_icons_[i] < 0)
      continue;
    add(20 + i, slots[kX2SlotPower1 + i], 20,
        static_cast<TouchAction>(static_cast<int>(TouchAction::Power1) + i),
        false);
    zones_.back().power_icon = power_icons_[i];
  }
  add(40, port_menu_rect(slots[kX2SlotPortMenu]), 20, TouchAction::PortMenu,
      false);

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

  std::vector<lucent::touch::Zone> router_zones;
  router_zones.reserve(zones_.size());
  for (const auto &zone : zones_)
    router_zones.push_back(zone.zone);
  router_.set_zones(router_zones);

  // Drawn HUD bounds, never quarters inferred from a layout slot. A
  // separate router lets their changing bounds cancel only HUD captures.
  router_zones.clear();
  const auto add_hud = [&](std::uint32_t first_id,
                           std::span<const X2Rect> regions, unsigned mask,
                           std::span<const TouchAction> actions) {
    for (std::uint32_t i = 0; i < regions.size(); ++i) {
      if (!(mask & (1u << i)))
        continue;
      add(first_id + i, regions[i], 30, actions[i], false);
      router_zones.push_back(zones_.back().zone);
    }
  };
  const std::array heroes{TouchAction::SelectHero1, TouchAction::SelectHero2,
                          TouchAction::SelectHero3, TouchAction::SelectHero4};
  // The retail potions, each in its own ring: a tap uses that potion.
  const std::array potions{TouchAction::HealthPack, TouchAction::EnergyPack};
  const std::array menus{TouchAction::RetailPauseMenu,
                         TouchAction::RetailTeamMenu};
  add_hud(50, hud_.portraits, hud_.portrait_mask, heroes);
  add_hud(60, hud_.potions, hud_.potion_mask, potions);
  add_hud(70, hud_.menu_icons, hud_.menu_icon_mask, menus);
  hud_router_.set_zones(router_zones);
}

std::vector<ActionEvent>
TouchControls::route(std::span<const lucent::touch::Contact> contacts) {
  std::vector<lucent::touch::Event> events;
  for (const auto &contact : contacts) {
    auto routed = hud_router_.route(std::span{&contact, 1});
    if (routed.empty())
      routed = router_.route(std::span{&contact, 1});
    events.insert(events.end(), routed.begin(), routed.end());
  }
  return translate(events);
}

std::vector<ActionEvent> TouchControls::cancel() {
  auto events = router_.cancel();
  const auto portraits = hud_router_.cancel();
  events.insert(events.end(), portraits.begin(), portraits.end());
  return translate(events);
}

std::vector<ActionEvent>
TouchControls::translate(std::span<const lucent::touch::Event> events) {
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
        add_stick_events(actions, event, stick_, stick_ring_,
                         {TouchAction::Forward, TouchAction::Backward,
                          TouchAction::MoveLeft, TouchAction::MoveRight});
      }
    } else if (event.zone_id == camera_swipe) {
      add_camera_events(actions, event, found->zone);
    } else if (clicks_retail_pointer(found->action)) {
      auto selected = event;
      // The retail mouse handler acts on these. Its fixed logical hit boxes
      // need the drawn centre even when mobile scaling makes the touch region
      // larger; moving a captured finger never drags a hero.
      selected.position = {(found->zone.left + found->zone.right) * 0.5F,
                           (found->zone.top + found->zone.bottom) * 0.5F};
      add_button_events(actions, selected, found->action);
    } else {
      add_button_events(actions, event, found->action);
    }
  }
  return actions;
}

X2Rect TouchControls::stick_ring() const {
  if (!stick_.engaged()) {
    return stick_ring_;
  }
  const float half_w = (stick_ring_.right - stick_ring_.left) * 0.5F;
  const float half_h = (stick_ring_.bottom - stick_ring_.top) * 0.5F;
  const lucent::touch::Point centre = stick_.centre();
  return {centre.x - half_w, centre.y - half_h, centre.x + half_w,
          centre.y + half_h};
}

} // namespace x2::input
