#ifndef X2_CONTROL_MENU_ROUTE_HPP
#define X2_CONTROL_MENU_ROUTE_HPP

#include "../input/touch_menu.hpp"
#include "platform_socket.h"
#include "retail_menu_model.hpp"

#include <string>

namespace x2::control {

/* The snapshot as the JSON GET /menu returns. */
std::string menu_json(const menu::MenuSnapshot &menu);

/* The touch menu as drawn: its rows and every button's output rectangle. */
std::string touch_menu_json(const input::TouchMenuState &state);

/* GET /menu: the active retail menu, read on the server thread. */
void menu_route(x2_socket_t fd);

} // namespace x2::control

#endif
