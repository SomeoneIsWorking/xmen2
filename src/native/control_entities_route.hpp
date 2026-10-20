#pragma once

#include "platform_socket.h"

namespace x2::control {

/* GET /entities[?match=TEXT]: every live entity's handle, definition and world
   position, read at the guest input poll; `match` keeps definitions containing
   TEXT, ignoring case. */
void entities_route(x2::native::Socket fd, const char *query);

} // namespace x2::control
