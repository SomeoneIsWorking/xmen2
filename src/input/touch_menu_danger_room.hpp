#ifndef X2_TOUCH_MENU_DANGER_ROOM_HPP
#define X2_TOUCH_MENU_DANGER_ROOM_HPP

#include "touch_menu_view.hpp"

namespace x2::input {

/* CMenuDangerRoom: the step's tabs (Overview and Status once a grade is
   chosen), one row per entry of its list (grades, then courses) and the
   selected entry's description. A tab is clicked where
   CMenuDangerRoom::onMouse (0x005b5a90) tests it. A tap on an entry walks the
   list's selection to it with the menu pad; a tap on the selected entry
   presses A, which takes the next step (0x005b6240). */
std::optional<TouchMenuView>
build_danger_room_view(const menu::MenuSnapshot &menu,
                       const presentation::RetailScenePlane &plane);

} // namespace x2::input

#endif
