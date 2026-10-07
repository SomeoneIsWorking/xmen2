#ifndef X2_TOUCH_MENU_PARTS_HPP
#define X2_TOUCH_MENU_PARTS_HPP

#include "touch_menu_view.hpp"

#include <span>
#include <string>
#include <string_view>
#include <vector>

/* What every class's touch menu view is built from. */
namespace x2::input {

/* The centre of the box the game hit-tests, in client coordinates. */
presentation::ClientPoint
menu_item_centre(const menu::SceneRect &rect,
                 const presentation::RetailScenePlane &plane);

/* Retail text split at the game's own line breaks, each line as
   touch_menu_text reads it; empty lines are dropped. */
std::vector<std::string> menu_text_lines(std::string_view raw);

/* The item named `name`, or null. */
const menu::MenuItem *find_menu_item(const menu::MenuSnapshot &menu,
                                     std::string_view name);

/* Enabled and not hidden. */
bool menu_item_shown(const menu::MenuItem &item);

/* The menu's own label_<name> item, else its first title* item. */
std::string menu_title(const menu::MenuSnapshot &menu);

/* Every shown desctext item whose text carries a $MENU_ token, one per
   token. */
void append_menu_footers(const menu::MenuSnapshot &menu,
                         const presentation::RetailScenePlane &plane,
                         TouchMenuView *view);

/* The shown items named in `names` that have text as tabs, lit as the game
   lights its open one (item+0x54 bit 0), each clicked on its own box. */
void append_menu_tabs(const menu::MenuSnapshot &menu,
                      const presentation::RetailScenePlane &plane,
                      std::span<const std::string_view> names,
                      TouchMenuView *view);

/* What a tap on a list entry asks of the game. Both walk the list's
   selection with the menu pad: a click on a CMenuShop entry is lost while a
   gear entry is selected, the pad's Up/Down and A are not. */
enum class ListTap {
  /* Select it; a tap on the selected entry presses A, which accepts it. */
  select,
  /* Select it and press A on arrival. */
  accept,
};

/* One row per entry of a list box (CMenuItemListBox or ListCodex), its
   further columns as the row's value. The list's Up/Down does not wrap. */
void append_list_entries(const menu::MenuItem &list, ListTap tap,
                         TouchMenuView *view);

/* A view for `menu` with no rows yet. */
TouchMenuView start_menu_view(const menu::MenuSnapshot &menu);

} // namespace x2::input

#endif
