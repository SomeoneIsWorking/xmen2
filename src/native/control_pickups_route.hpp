#pragma once

#include "platform_socket.h"

namespace x2::control {

/* GET /pickups[?handle=N]: every dropped item's entity, prompt label, model
   instance and loop effect, beside the model pools a new model is refused
   from when one is full, read at the guest input poll. */
void pickups_route(x2::native::Socket fd, const char *query);

} // namespace x2::control
