#ifndef X2_MENU_ITEM_TAP_HPP
#define X2_MENU_ITEM_TAP_HPP

#include "retail_menu_model.hpp"

#include <cstdint>

/* What the pointer does to a retail menu item that has no usecmd, which
   CMenuItem::onMouse (0x005bc1b0) leaves untouchable (docs/RE/menus.md). */
namespace x2::native {

inline constexpr std::uint32_t kMouseButtonUp = 0x202u;

struct MenuItemTap {
  /* onMouse's answer: CMenu::onMouse focuses the item. */
  bool hit = false;
  /* Publish MENU_ACCEPT, as a click on a row with a usecmd does. */
  bool accept = false;
};

/* A row the game navigates to: a text row in the up/down chain. */
bool menu_tap_row(const menu::MenuSnapshot &menu, int item);

/* The item at `item` under scene point (x, z) for mouse message `message`:
   a tap row is focused, and chosen on button up unless it steps left/right.
   Anything else is not hit. */
MenuItemTap menu_item_tap(const menu::MenuSnapshot &menu, int item, int x,
                          int z, std::uint32_t message);

} // namespace x2::native

#endif
