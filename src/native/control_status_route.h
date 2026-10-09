#pragma once

#include "platform_socket.h"

namespace x2::native {

void control_status_route(x2::native::Socket fd, unsigned long requests,
                          unsigned long keys_pressed,
                          unsigned long keys_refused,
                          unsigned long screenshots);

} // namespace x2::native
