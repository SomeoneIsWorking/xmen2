#ifndef X2_TEAM_TOUCH_HPP
#define X2_TEAM_TOUCH_HPP

#include "x86rt.h"

#include <cstdint>
#include <string_view>

/* CMenuTeam's detail tabs on touch (docs/touch-play.md): finger-sized
   controls of the game's option art in the column where the hero stands,
   each drawn by the retail text item it belongs to, and the skill Details
   view held by a tap instead of a held finger. */
namespace x2::native {

/* CMenuTeam's text item `self` is drawing: on touch the column parts it owns
   are drawn and put as touch targets, otherwise taken away. Any other
   menu's item is left alone. */
void team_touch_text_draw(const CPU &cpu, std::uint32_t self, bool touch);
/* The text item `self` is destroyed: its parts go with it. */
void team_touch_text_destroy(const CPU &cpu, std::uint32_t self);
/* Whether touch leaves CMenuTeam's prompt `text` undrawn: the skills tab's
   Assign, which the column draws, and every prompt while a skill is being
   assigned, which the column's power buttons replace. */
bool team_touch_hides(std::uint32_t self, std::string_view text);

} // namespace x2::native

#endif
