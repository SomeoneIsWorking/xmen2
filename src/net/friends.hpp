#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
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
  // Where this machine's meeting is, as a player sees it.
  enum class Stage : std::uint8_t {
    Idle,       // no meeting
    Gathering,  // finding this machine's addresses for its code
    Waiting,    // showing a code for the other player to paste
    Connecting, // both codes known, trying the ways through
    Connected,  // the last meeting added a friend
    Failed,     // the last meeting ended without one
  };
  struct Shown {
    Stage stage = Stage::Idle;
    bool hosting = false;
    // The code this machine shows (an invite, or the reply to one), spaced as
    // the player reads it.
    std::string code;
    // Why the last meeting failed.
    std::string problem;
    std::size_t friends = 0;
  };

  Friends();
  ~Friends();
  Friends(const Friends &) = delete;
  Friends &operator=(const Friends &) = delete;
  Friends(Friends &&) = delete;
  Friends &operator=(Friends &&) = delete;

  // Where friends' datagrams go; none in a browser page.
  FriendRoute *route();

  // Starts this machine's ZeroTier node once, so it is on its network before a
  // code is made or pasted: identity in `storage`, `roots` a signed root set
  // (empty for ZeroTier's public roots). A node that cannot start leaves
  // meetings to the other ways through.
  void go_online(const std::filesystem::path &storage,
                 const std::string &roots);

  // A new invite; false with `error` while a meeting runs or where there are no
  // friends.
  bool invite(std::string &error);
  // A code the other player shows: their invite, or their reply to this
  // machine's invite.
  bool paste(std::string_view code, std::string &error);
  // Takes a finished meeting's friend.
  void poll();
  Shown shown() const;
  // The meeting's step and code, and the friends so far, for /lan.
  std::string status() const;

private:
  struct State;
  std::unique_ptr<State> state_;
};

} // namespace x2::net
