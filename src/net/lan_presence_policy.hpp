#ifndef X2_LAN_PRESENCE_POLICY_HPP
#define X2_LAN_PRESENCE_POLICY_HPP

/*
 * What this machine announces, and what it has heard (issue #188).
 *
 * Pure decisions over facts the runtime reads from the game, so every rule
 * is tested without a guest: whether the loaded map is a campaign map, what
 * phase (if any) to announce, and which announcing peers are still alive.
 */

#include "lan_presence_protocol.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace x2::net::presence {

/* A map a saved campaign can resume in: the story acts and the briefings
   between them. The front end, the Danger Room, versus, and bonus maps are
   not campaigns. Case-insensitive, as the engine's own map compares are. */
bool is_campaign_map(std::string_view map);

struct LocalFacts {
  /* The path of the last map that loaded successfully. */
  std::string map;
  /* This machine joined the network session through another's lobby. */
  bool client = false;
  /* The session director waits in its own lobby. */
  bool hosting_lobby = false;
  /* Part way through a re-form or a join, or waiting for a host's lobby. */
  bool directing = false;
  /* Players in the network session; none outside one. */
  uint8_t session_players = 0u;
};

/* The Announce this machine makes, or none while it is not joinable: at the
   front end, a client of someone else, or part way through a re-form. */
std::optional<Announce> local_announce(const LocalFacts &facts,
                                       const std::string &name);

struct Peer {
  uint64_t instance = 0u;
  Announce announce;
  double heard = 0.0;
};

class PeerTable {
public:
  /* A peer silent this long has gone (three missed announcements). */
  static constexpr double kExpirySeconds = 3.5;

  /* Record an Announce. True when the visible list changed. */
  bool heard(uint64_t instance, const Announce &announce, double now);

  /* Forget silent peers. True when the visible list changed. */
  bool expire(double now);

  /* Oldest-heard first, so a row does not jump between two hosts. */
  const std::vector<Peer> &peers() const { return peers_; }

  const Peer *find(uint64_t instance) const;

private:
  std::vector<Peer> peers_;
};

} // namespace x2::net::presence

#endif
