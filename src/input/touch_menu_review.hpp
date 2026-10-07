#ifndef X2_TOUCH_MENU_REVIEW_HPP
#define X2_TOUCH_MENU_REVIEW_HPP

#include "touch_menu_view.hpp"

namespace x2::input {

/* CMenuReviewPaths: its screens/cinematics/comics/concepts/stats tabs, then
   one row per entry of its list, a stats entry with its count. A tab is
   clicked where CMenuReviewPaths::onMouse (0x005d04d0) tests it. A tap on an
   entry walks the list's selection to it with the menu pad; a tap on the
   selected entry presses A, which shows it when the game allows
   (0x005d18a0). */
std::optional<TouchMenuView>
build_review_view(const menu::MenuSnapshot &menu,
                  const presentation::RetailScenePlane &plane);

} // namespace x2::input

#endif
