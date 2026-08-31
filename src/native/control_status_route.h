#ifndef X2_CONTROL_STATUS_ROUTE_H
#define X2_CONTROL_STATUS_ROUTE_H

void control_status_route(int fd, unsigned long requests,
                          unsigned long keys_pressed, unsigned long keys_refused,
                          unsigned long screenshots);

#endif
