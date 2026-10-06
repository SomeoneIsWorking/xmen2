#ifndef X2_TOUCH_MENU_SHOP_HPP
#define X2_TOUCH_MENU_SHOP_HPP

#include "touch_menu_view.hpp"

namespace x2::input {

/* CMenuShop as a shop (not the stash): its buy/sell/training tabs, then one
   row per entry of its list box. A tab is clicked where CMenuShop::onMouse
   (0x005d3400) tests it. An entry the list's window shows is clicked on its
   row (0x005c0e10): a click selects it, a click on the selected entry buys.
   An entry outside the window is walked to with the menu pad and selected. */
std::optional<TouchMenuView>
build_shop_view(const menu::MenuSnapshot &menu,
                const presentation::RetailScenePlane &plane);

} // namespace x2::input

#endif
