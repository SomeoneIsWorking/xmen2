#ifndef X2_TOUCH_MENU_PARTS_HPP
#define X2_TOUCH_MENU_PARTS_HPP

#include "touch_menu_view.hpp"

#include <string>
#include <string_view>

/* What every class's touch menu view is built from. */
namespace x2::input {

/* The centre of the box the game hit-tests, in client coordinates. */
presentation::ClientPoint
menu_item_centre(const menu::SceneRect &rect,
                 const presentation::RetailScenePlane &plane);

/* The item named `name`, or null. */
const menu::MenuItem *find_menu_item(const menu::MenuSnapshot &menu,
                                     std::string_view name);

/* Enabled and not hidden. */
bool menu_item_shown(const menu::MenuItem &item);

/* The menu's own label_<name> item, else its first title* item. */
std::string menu_title(const menu::MenuSnapshot &menu);

/* Every shown desctext item whose text carries a $MENU_ token. */
void append_menu_footers(const menu::MenuSnapshot &menu,
                         const presentation::RetailScenePlane &plane,
                         TouchMenuView *view);

/* A view for `menu` with no rows yet. */
TouchMenuView start_menu_view(const menu::MenuSnapshot &menu);

} // namespace x2::input

#endif
