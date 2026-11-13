#ifndef X2_MENU_SCENE_HPP
#define X2_MENU_SCENE_HPP

#include "../input/touch_cards.hpp"
#include "guest_call.hpp"

#include <cstdint>
#include <string_view>

/* The game's own model entities placed in a menu by port code: loading a
   model's art, attaching it to CMenuMgr's scene, placing and stretching it,
   and the menu style's text colour (external/xmen2-decomp/docs/notes/menus.md). */
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
/* Art's extents about its origin at scale 1: across and up, and the depth
   of its nearest face (the option art's reaches 8 or 9 units in front). */
struct Art {
  input::SceneBox box;
  float front = 0.0F;
};
/* Art stretched over `box` with its nearest face at `depth`; an empty box
   parks it off screen. */
void stretch(guest::GuestCall &call, Vectors &vectors, std::uint32_t entity,
             const Art &art, const input::SceneBox &box, float depth);
/* The entity's extents, placed at its origin at scale 1. */
Art bounds(guest::GuestCall &call, Vectors &vectors, std::uint32_t entity);

/* The entity's animation shown at `phase` of its length, 0 to 1; art
   without one is left alone. */
void show_phase(guest::GuestCall &call, std::uint32_t entity, float phase);

/* A menu style's packed text colour (FUN_005d8ae0): `lit` from 0 plain to
   1 selected, `custom` the entry's own colour or 0. */
std::uint32_t style_color(guest::GuestCall &call, Vectors &vectors,
                          std::uint32_t style, float lit, bool enabled,
                          std::uint32_t item = 0u, std::uint32_t custom = 0u);

/* A menu style's igVec4f colour as CMenuItemBar reads it (CMenuMgr
   vt+0x224), into `rgba`. */
void style_rgba(guest::GuestCall &call, std::uint8_t style, float rgba[4]);

/* One quad of the menu sprite sheet `texture` queued as CMenuItemBar's draw
   queues its fill (FUN_005ee180 on CMenuMgr vt+0x220): `box` in scene units
   at `depth`, tinted `rgba`. */
void queue_sprite(guest::GuestCall &call, Vectors &vectors,
                  const input::SceneBox &box, float depth, const float rgba[4],
                  std::uint8_t texture);

/* The igVec4f colour at `rgba` packed as the text calls take it. */
std::uint32_t pack_color(guest::GuestCall &call, std::uint32_t rgba);

inline constexpr float kHidden = -9999.0F;

} // namespace x2::native::menu_scene

#endif
