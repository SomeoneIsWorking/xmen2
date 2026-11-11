#include "lan_broadcast.hpp"

#include <lucent/network_interfaces.h>

#include <array>
#include <cstring>

namespace x2::net {
namespace {

/* The address's bytes most significant first, as they sit in a sockaddr. */
std::uint32_t network_order(std::uint32_t host) {
  const std::array<std::uint8_t, 4> bytes = {
      static_cast<std::uint8_t>(host >> 24u),
      static_cast<std::uint8_t>(host >> 16u),
      static_cast<std::uint8_t>(host >> 8u), static_cast<std::uint8_t>(host)};
  std::uint32_t out = 0;
  std::memcpy(&out, bytes.data(), sizeof out);
  return out;
}

} // namespace

std::vector<std::uint32_t> lan_broadcast_destinations(std::string &error) {
  std::vector<std::uint32_t> out;
  if (const auto interfaces = lucent::net::ipv4_interfaces(error)) {
    for (const std::uint32_t broadcast :
         lucent::net::lan_broadcasts(*interfaces)) {
      out.push_back(network_order(broadcast));
    }
  }
  if (out.empty()) {
    out.push_back(0xffffffffu);
  }
  return out;
}

} // namespace x2::net
