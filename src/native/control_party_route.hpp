#ifndef X2_CONTROL_PARTY_ROUTE_HPP
#define X2_CONTROL_PARTY_ROUTE_HPP

#include "platform_socket.h"

namespace x2::control {

/* GET /party: the party's money and each hero's health and energy, read from
   the game's own memory at the guest input poll (no guest call). */
void party_route(x2_socket_t fd);

} // namespace x2::control

#endif
