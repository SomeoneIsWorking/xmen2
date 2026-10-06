#ifndef X2_TOUCH_RUNTIME_MENU_HPP
#define X2_TOUCH_RUNTIME_MENU_HPP

#include "touch_menu.hpp"

#include <optional>

namespace x2::input {

/* The touch runtime's side of the touch menu: the latest view of the retail
   menu goes in, the drawn state comes out. Guest-thread calls, except
   touch_menu_state, which any thread may call. */
void touch_runtime_set_menu(std::optional<TouchMenuView> view);
TouchMenuState touch_menu_state();

} // namespace x2::input

#endif
