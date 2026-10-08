#ifndef X2_CONTROL_MENU_ROUTE_HPP
#define X2_CONTROL_MENU_ROUTE_HPP

#include "../input/touch_menu.hpp"
#include "platform_socket.h"
#include "retail_menu_model.hpp"

#include <string>

namespace x2::control {

/* The snapshot as the JSON GET /menu returns; all_items adds every item. */
std::string menu_json(const menu::MenuSnapshot &menu, bool all_items);

/* The touch menu as drawn: its rows and every button's output rectangle. */
std::string touch_menu_json(const input::TouchMenuState &state);

/* GET /menu[?items=all]: the active retail menu, read at the guest input
   poll so it never sees a menu half-updated. */
void menu_route(x2::native::Socket fd, const char *query);

} // namespace x2::control

#endif
