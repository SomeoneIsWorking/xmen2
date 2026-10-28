#ifndef X2_MENU_SCENE_HPP
#define X2_MENU_SCENE_HPP

#include "../input/touch_cards.hpp"
#include "guest_call.hpp"

#include <cstdint>
#include <string_view>

/* The game's own model entities placed in a menu by port code: loading a
   model's art, attaching it to CMenuMgr's scene, placing and stretching it,
   and the menu style's text colour (docs/RE/menus.md). */
namespace x2::native::menu_scene {

/* Guest scratch for one draw's vectors. */
class Vectors {
public:
  Vectors() : block_(64u) {}
  std::uint32_t put(int slot, float a, float b, float c, float d = 0.0F);
  explicit operator bool() const { return static_cast<bool>(block_); }

private:
  guest::GuestBlock block_;
};

/* An entity of the model `name`, as CMenuItemListBox loads its cursor; 0
   when the art is missing. */
std::uint32_t load_model(guest::GuestCall &call, std::string_view name);
/* Drops the reference load_model took. */
void release(guest::GuestCall &call, std::uint32_t entity);

/* CMenuMgr's scene draws the entity, or stops. */
void attach(guest::GuestCall &call, std::uint32_t entity);
void detach(guest::GuestCall &call, std::uint32_t entity);

/* The entity at `at` and `depth`, scaled across and up, turned about the
   depth axis. */
void place(guest::GuestCall &call, Vectors &vectors, std::uint32_t entity,
           presentation::ScenePoint at, float depth, float sx, float sz,
           float turn = 0.0F);
/* Art whose extents are `art` at scale 1 stretched over `box`; an empty box
   parks it off screen. */
void stretch(guest::GuestCall &call, Vectors &vectors, std::uint32_t entity,
             const input::SceneBox &art, const input::SceneBox &box,
             float depth);
/* The entity's extents about its origin, placed there at scale 1. */
input::SceneBox bounds(guest::GuestCall &call, Vectors &vectors,
                       std::uint32_t entity);

/* The entity's animation shown at `phase` of its length, 0 to 1; art
   without one is left alone. */
void show_phase(guest::GuestCall &call, std::uint32_t entity, float phase);

/* A menu style's packed text colour (FUN_005d8ae0): `lit` from 0 plain to
   1 selected, `custom` the entry's own colour or 0. */
std::uint32_t style_color(guest::GuestCall &call, Vectors &vectors,
                          std::uint32_t style, float lit, bool enabled,
                          std::uint32_t item = 0u, std::uint32_t custom = 0u);

/* The igVec4f colour at `rgba` packed as the text calls take it. */
std::uint32_t pack_color(guest::GuestCall &call, std::uint32_t rgba);

inline constexpr float kHidden = -9999.0F;

} // namespace x2::native::menu_scene

#endif
