#pragma once

#include <lucent/ipv4.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace x2::net {

// What the Winsock layer and the presence socket see of the friends added by
// code (friends.hpp): each friend is a guest-visible IPv4 address, so neither
// links the transport.
class FriendRoute {
public:
  FriendRoute() = default;
  FriendRoute(const FriendRoute &) = delete;
  FriendRoute &operator=(const FriendRoute &) = delete;
  FriendRoute(FriendRoute &&) = delete;
  FriendRoute &operator=(FriendRoute &&) = delete;
  virtual ~FriendRoute() = default;

  virtual bool is_friend(std::uint32_t address) const = 0;
  // `bytes` from the guest's `from_port` to `to`; false when `to` is no friend
  // or the path refused it.
  virtual bool send(const lucent::net::Endpoint &to, std::uint16_t from_port,
                    std::string_view bytes) = 0;
  // To `to_port` on every friend; how many took it.
  virtual std::size_t broadcast(std::uint16_t to_port, std::uint16_t from_port,
                                std::string_view bytes) = 0;
  // The friend endpoint a datagram from 127.0.0.1:`loopback_port` stands for.
  virtual std::optional<lucent::net::Endpoint>
  source_of(std::uint16_t loopback_port) const = 0;
};

} // namespace x2::net
