#ifndef X2_CONTROL_MENU_ROUTE_HPP
#define X2_CONTROL_MENU_ROUTE_HPP

#include "../presentation/retail_scene_plane.hpp"
#include "platform_socket.h"
#include "retail_menu_model.hpp"

#include <string>

namespace x2::control {

/* The snapshot as the JSON GET /menu returns; all_items adds every item, and
   a scene plane adds each item's tap point. */
std::string menu_json(const menu::MenuSnapshot &menu, bool all_items,
                      const presentation::RetailScenePlane *plane);

/* GET /menu[?items=all]: the active retail menu, read at the guest input
   poll so it never sees a menu half-updated. */
void menu_route(x2::native::Socket fd, const char *query);

/* GET /menu/action?name=<token>: the `$MENU_<TOKEN>` action posted at the
   guest input poll, as a click on a footer carrying it posts it, so the
   active menu's own update acts on it (Options' ok opens Advanced Options). */
void menu_action_route(x2::native::Socket fd, const char *query);

} // namespace x2::control

#endif
