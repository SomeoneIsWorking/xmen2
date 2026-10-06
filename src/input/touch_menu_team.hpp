#ifndef X2_TOUCH_MENU_TEAM_HPP
#define X2_TOUCH_MENU_TEAM_HPP

#include "touch_menu_view.hpp"

namespace x2::input {

/* CMenuTeam's party screen (mode 0): one row per hero summary. A click on a
   hero selects it and a click on the selected hero opens its details
   (CMenuTeam::onMouse, 0x005e25c0). Other modes keep the retail screen. */
std::optional<TouchMenuView>
build_team_view(const menu::MenuSnapshot &menu,
                const presentation::RetailScenePlane &plane);

} // namespace x2::input

#endif
