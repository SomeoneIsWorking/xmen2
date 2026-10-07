#ifndef X2_TOUCH_MENU_TEAM_DETAILS_HPP
#define X2_TOUCH_MENU_TEAM_DETAILS_HPP

#include "touch_menu_view.hpp"

namespace x2::input {

/* CMenuTeam's hero detail tabs (modes 2..6): stats, skills, gear and ai under
   the game's own tab bar, each tab clicked on its own item. Every tab's rows
   are walked with the menu pad, and a tap on the current row presses A: add
   a stat point or a skill rank, equip, or change an ai setting. */
std::optional<TouchMenuView>
build_team_details_view(const menu::MenuSnapshot &menu,
                        const presentation::RetailScenePlane &plane);

/* A skill entry's rank column, the game's run of rank glyphs (0xd9 owned,
   0xda added in this visit, 0xd8 open at this level, 0xd7 above it, 0xdb
   filler), as "rank owned/ranks"; nullopt for any other column. */
std::optional<std::string> skill_rank_text(std::string_view column);

} // namespace x2::input

#endif
