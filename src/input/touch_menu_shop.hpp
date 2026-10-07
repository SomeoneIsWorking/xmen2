#ifndef X2_TOUCH_MENU_SHOP_HPP
#define X2_TOUCH_MENU_SHOP_HPP

#include "touch_menu_view.hpp"

namespace x2::input {

/* CMenuShop as the shop or the stash: its buy/sell/training or
   stash/inventory tabs, then one row per entry of its list box. A tab is
   clicked where CMenuShop::onMouse (0x005d3400) tests it. A tap on an entry
   walks the list's selection to it with the menu pad; a tap on the selected
   entry presses A, which buys, sells, trains, or moves it between stash and
   inventory. */
std::optional<TouchMenuView>
build_shop_view(const menu::MenuSnapshot &menu,
                const presentation::RetailScenePlane &plane);

} // namespace x2::input

#endif
