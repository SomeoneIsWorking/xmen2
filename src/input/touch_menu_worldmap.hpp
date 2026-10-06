#ifndef X2_TOUCH_MENU_WORLDMAP_HPP
#define X2_TOUCH_MENU_WORLDMAP_HPP

#include "touch_menu_view.hpp"

namespace x2::input {

/* CMenuWorldMap: the acts it has unlocked as tabs, the open act's unlocked
   extraction points as rows, and the region name and the selected point's
   description below them. A tab is clicked where CMenuWorldMap::onMouse
   (0x005e7f10) tests it. A tap on a point walks the game's focus to it with
   the menu pad; a tap on the selected point presses A, which travels there
   (0x005e8d90). Its footers are the game's. */
std::optional<TouchMenuView>
build_worldmap_view(const menu::MenuSnapshot &menu,
                    const presentation::RetailScenePlane &plane);

} // namespace x2::input

#endif
