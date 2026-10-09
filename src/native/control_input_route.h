#pragma once

#include "control_http.h"

namespace x2::native {

/* The control channel's input-driving routes. Each parses its own query and
   writes its own reply; the work is done on the guest-input thread through
   control_command_bridge.h. */
void control_route_key(x2::native::Socket fd, const char *query);
void control_route_pad(x2::native::Socket fd, const char *query);
void control_route_touch(x2::native::Socket fd, const char *query);
void control_route_assignment(x2::native::Socket fd, const char *query);

void control_route_controls(x2::native::Socket fd);

} // namespace x2::native
