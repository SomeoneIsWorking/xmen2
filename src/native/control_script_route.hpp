#pragma once

#include "platform_socket.h"

namespace x2::control {

/* GET /script?text=CODE: write CODE as a script file in this run's generated
   asset overlay and queue `runscript` on it. The retail runscript takes one
   whitespace-free token (0x005f2350), so code with spaces or several
   statements has to arrive as a file. */
void script_route(x2::native::Socket fd, const char *query);

} // namespace x2::control
