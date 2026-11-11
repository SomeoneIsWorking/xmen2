#ifndef X2_LAN_BROADCAST_HPP
#define X2_LAN_BROADCAST_HPP

#include <cstdint>
#include <string>
#include <vector>

namespace x2::net {

/* Where a datagram for the whole LAN goes, in network byte order: every
   running interface's directed broadcast, so a virtual LAN that is not the
   default route (ZeroTier, a VPN) hears it too. With no such interface it is
   the limited broadcast. `error` names a failed interface query. */
std::vector<std::uint32_t> lan_broadcast_destinations(std::string &error);

} // namespace x2::net

#endif
