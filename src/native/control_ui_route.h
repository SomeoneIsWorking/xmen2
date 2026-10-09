#pragma once

#include "platform_socket.h"

namespace x2::native {

/* Press a key, or click, at the PORT's own UI layer rather than the guest's.
   See the .c: the game's input is injected into DirectInput and can never
   reach an SDL document. */
void control_ui_key_route(x2::native::Socket fd, const char *query);
void control_ui_click_route(x2::native::Socket fd, const char *query);

} // namespace x2::native
