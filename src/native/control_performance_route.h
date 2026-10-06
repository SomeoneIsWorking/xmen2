#ifndef X2_CONTROL_PERFORMANCE_ROUTE_H
#define X2_CONTROL_PERFORMANCE_ROUTE_H

#include "platform_socket.h"

void control_performance_reset_route(x2_socket_t fd);

/* /performance/probe?n=<count>: arm the hot-guest-entry-point probe, or
   disarm it with n=0. The released build has no environment to set X2_HOTEP
   in, and reinstalling a debug build to get one costs the player's data. */
void control_performance_probe_route(x2_socket_t fd, const char *query);

#endif
