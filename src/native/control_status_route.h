#ifndef X2_CONTROL_STATUS_ROUTE_H
#define X2_CONTROL_STATUS_ROUTE_H

#include "platform_socket.h"

void control_status_route(x2_socket_t fd, unsigned long requests,
                          unsigned long keys_pressed,
                          unsigned long keys_refused,
                          unsigned long screenshots);

#endif
