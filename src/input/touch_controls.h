#ifndef X2_TOUCH_CONTROLS_H
#define X2_TOUCH_CONTROLS_H

#include "../presentation/touch_layout.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include <lucent/touch.h>

namespace x2::input {

enum class TouchAction : std::uint8_t {
  Forward,
  Backward,
  MoveLeft,
  MoveRight,
  LightAttack,
  HeavyAttack,
  Jump,
  Use,
  Powers,
  EnergyPack,
  HealthPack,
  NextHero,
  PreviousHero,
  DecreaseAggr,
  IncreaseAggr,
  MapToggle,
  Pause,
  Stats,
  CameraUp,
  CameraDown,
  CameraLeft,
  CameraRight,
  SelectHero1,
  SelectHero2,
  SelectHero3,
  SelectHero4,
};

struct SafeArea {
  float left = 0.0F;
  float top = 0.0F;
  float right = 0.0F;
  float bottom = 0.0F;
};

struct Viewport {
  float width = 0.0F;
  float height = 0.0F;
  SafeArea safe_area;
};

struct ActionEvent {
  std::int64_t contact_id = 0;
  std::uint32_t zone_id = 0;
  TouchAction action = TouchAction::Pause;
  float value = 0.0F;
  lucent::touch::Point position;
  lucent::touch::Phase phase = lucent::touch::Phase::moved;
};

std::optional<float> touch_axis_value(std::span<const ActionEvent> events,
                                      TouchAction negative,
                                      TouchAction positive);

// Retail has one mouse button: the first portrait finger owns that pointer
// until released or canceled. Other held fingers never take over implicitly;
// they must lift and begin again. Every selected transition is returned in
// order.
class PortraitPointer {
public:
  std::vector<ActionEvent> route(std::span<const ActionEvent> events);

private:
  std::optional<std::int64_t> contact_;
};

// Title-specific virtual controls. Layout and action vocabulary live here;
// platform event acquisition, rendering feedback, and guest input publication
// remain outside this owner.
class TouchControls {
public:
  struct ZoneVisual {
    lucent::touch::Zone zone;
    TouchAction action = TouchAction::Pause;
    bool stick = false;
    bool visible = true;
  };

  // Returns cancellation events for contacts captured under the old layout. The
  // caller must publish those events before applying the new layout so a
  // rotation cannot leave an action pressed in the guest.
  std::vector<ActionEvent> set_viewport(Viewport viewport);
  // Actual output-pixel bounds published by the native HUD. Empty or invalid
  // regions remove portrait captures; changing them leaves other controls held.
  std::vector<ActionEvent> set_portraits(std::span<const X2Rect> portraits,
                                         unsigned visible_mask);
  std::vector<ActionEvent>
  route(std::span<const lucent::touch::Contact> contacts);
  std::vector<ActionEvent> cancel();
  std::span<const ZoneVisual> zones() const { return zones_; }

private:
  void rebuild_zones();
  std::vector<ActionEvent>
  translate(std::span<const lucent::touch::Event> events) const;

  Viewport viewport_;
  std::array<X2Rect, 4> portraits_{};
  unsigned portraits_visible_ = 0;
  std::vector<ZoneVisual> zones_;
  lucent::touch::Router router_;
  lucent::touch::Router portrait_router_;
};

} // namespace x2::input

#endif /* X2_TOUCH_CONTROLS_H */
