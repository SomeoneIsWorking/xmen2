#ifndef X2_TOUCH_CONTROLS_H
#define X2_TOUCH_CONTROLS_H

#include "../presentation/touch_layout.h"
#include "thumb_stick.h"

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
  // The hero's RT powers in the game's slot order: each is RT with A, B, X
  // or Y, exactly the chord the retail ring teaches.
  Power1,
  Power2,
  Power3,
  Power4,
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
  // The pause and team menu icons the game's mouse overlay draws at the top
  // centre. Like a portrait, a tap is a click on the retail icon.
  RetailPauseMenu,
  RetailTeamMenu,
  // The port's own RmlUi menu (F2 on a keyboard).
  PortMenu,
};

// True for an action that is a click on something the retail GUI drew, which
// its own mouse handler acts on, rather than a pad button.
constexpr bool clicks_retail_pointer(TouchAction action) {
  return (action >= TouchAction::SelectHero1 &&
          action <= TouchAction::SelectHero4) ||
         action == TouchAction::RetailPauseMenu ||
         action == TouchAction::RetailTeamMenu;
}

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

// Retail draws one cursor and has one mouse button, so one contact owns the
// pointer at a time: the first to begin, until it ends or is canceled. A
// finger arriving while it is held never takes over implicitly; it must lift
// and begin again. Both things that move retail's pointer -- a portrait tap
// in gameplay and a tap on the retail GUI when no control is drawn -- obey
// this one rule rather than each keeping its own copy of it.
class PointerOwner {
public:
  // True when this contact may move the pointer in this phase. An ending or
  // canceled phase is accepted and gives ownership up.
  bool accepts(std::int64_t contact_id, lucent::touch::Phase phase);
  bool held() const { return contact_.has_value(); }
  void release() { contact_.reset(); }

private:
  std::optional<std::int64_t> contact_;
};

// The portrait taps, which select a hero through that same pointer. Every
// selected transition is returned in order.
class PortraitPointer {
public:
  std::vector<ActionEvent> route(std::span<const ActionEvent> events);

private:
  PointerOwner owner_;
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
    // The power's atlas cell for a power zone, -1 for every other zone.
    int power_icon = -1;
  };

  // Returns cancellation events for contacts captured under the old layout. The
  // caller must publish those events before applying the new layout so a
  // rotation cannot leave an action pressed in the guest.
  std::vector<ActionEvent> set_viewport(Viewport viewport);
  // What the native HUD drew that a finger can press, in output pixels:
  // portraits, potions and the retail menu icons. A group with a rectangle
  // that is not a real on-screen area is dropped whole rather than routed
  // to; a change releases only HUD captures, never the stick or buttons.
  std::vector<ActionEvent> set_hud(const X2HudRegions &regions);
  // The atlas cell of each power slot, or -1 when the hero has no power
  // there; only slots with a power get a zone. A change releases captured
  // contacts, as a layout change does, so a power that vanishes under a
  // finger cannot stay held.
  std::vector<ActionEvent> set_power_icons(const std::array<int, 4> &icons);
  std::vector<ActionEvent>
  route(std::span<const lucent::touch::Contact> contacts);
  std::vector<ActionEvent> cancel();
  std::span<const ZoneVisual> zones() const { return zones_; }
  // What the movement ring should draw: its live deflection, -1..1 per axis.
  ThumbStick::Deflection stick_deflection() const {
    return stick_.deflection();
  }
  // Where the movement ring is drawn: centred on the thumb while one holds
  // it, at its layout position otherwise.
  X2Rect stick_ring() const;

private:
  void rebuild_zones();
  // Where the port menu goes: the next place in the game's menu-icon row,
  // once the game has drawn that row, and `waiting` until it has.
  X2Rect port_menu_rect(X2Rect waiting) const;
  unsigned accept_regions(std::span<const X2Rect> regions, unsigned mask,
                          std::span<X2Rect> out) const;
  std::vector<ActionEvent>
  translate(std::span<const lucent::touch::Event> events);

  ThumbStick stick_;
  X2Rect stick_ring_{};
  Viewport viewport_;
  std::array<int, 4> power_icons_{-1, -1, -1, -1};
  X2HudRegions hud_{};
  std::vector<ZoneVisual> zones_;
  lucent::touch::Router router_;
  lucent::touch::Router hud_router_;
};

} // namespace x2::input

#endif /* X2_TOUCH_CONTROLS_H */
