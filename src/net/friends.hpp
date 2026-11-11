#pragma once

#include <memory>
#include <string>
#include <string_view>

namespace x2::net {

class FriendRoute;

// Friends added by code: this machine shows an invite, or takes the code a
// friend shows (an invite, or the reply to its own), and each meeting that
// connects becomes a friend on the guest's LAN (lucent::p2p::VirtualLan). One
// meeting at a time. A browser page has no friends.
class Friends {
public:
  Friends();
  ~Friends();
  Friends(const Friends &) = delete;
  Friends &operator=(const Friends &) = delete;
  Friends(Friends &&) = delete;
  Friends &operator=(Friends &&) = delete;

  // Where friends' datagrams go; none in a browser page.
  FriendRoute *route();

  // A new invite; false with `error` while a meeting runs or where there are no
  // friends.
  bool invite(std::string &error);
  // A code the other player shows: their invite, or their reply to this
  // machine's invite.
  bool paste(std::string_view code, std::string &error);
  // Takes a finished meeting's friend.
  void poll();
  // The meeting's step and code, and the friends so far.
  std::string status() const;

private:
  struct State;
  std::unique_ptr<State> state_;
};

} // namespace x2::net
