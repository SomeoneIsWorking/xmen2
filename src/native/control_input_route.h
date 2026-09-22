#ifndef X2_CONTROL_INPUT_ROUTE_H
#define X2_CONTROL_INPUT_ROUTE_H

#include "control_http.h"

/* The control channel's input-driving routes. Each parses its own query and
   writes its own reply; the work is done on the guest-input thread through
   control_command_bridge.h. */
void control_route_key(x2_socket_t fd, const char *query);
void control_route_pad(x2_socket_t fd, const char *query);
void control_route_touch(x2_socket_t fd, const char *query);
void control_route_assignment(x2_socket_t fd, const char *query);

#endif
