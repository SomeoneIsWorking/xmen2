#ifndef X2_TOUCH_MENU_TEAM_HPP
#define X2_TOUCH_MENU_TEAM_HPP

#include "touch_menu_view.hpp"

namespace x2::input {

/* CMenuTeam's party (mode 0) and roster (mode 1). The party is one row per
   hero summary: a click on a hero selects it and a click on the selected hero
   opens its details (CMenuTeam::onMouse, 0x005e25c0). The roster is one row
   per hero the cards name, walked with the menu pad and chosen or revived
   with A. Modes 2..6 are the hero's detail tabs (touch_menu_team_details). */
std::optional<TouchMenuView>
build_team_view(const menu::MenuSnapshot &menu,
                const presentation::RetailScenePlane &plane);

} // namespace x2::input

#endif
