#ifndef X2_LAN_RENDEZVOUS_HPP
#define X2_LAN_RENDEZVOUS_HPP

/*
 * Pairing two game sockets on one LAN without GameSpy's NAT negotiation
 * servers (issue #188).
 *
 * XMen2.exe links every client of a session to every other one. When a
 * third player joins, the host hands the newcomer and each client already in
 * the game one cookie, and both sides ask natneg*.gamespy.com to introduce
 * them. Those servers are gone, so the port introduces them itself: each side
 * broadcasts its cookie, its client index and its game socket's endpoint
 * until it has heard the other side and the other side has said it heard.
 *
 * This class is only that exchange -- which messages are due, and how each
 * pairing ends. Sending, receiving and answering the game belong to the
 * caller.
 */

#include "lan_presence_protocol.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <vector>

namespace x2::net::presence {

struct Endpoint {
  std::array<uint8_t, 4> address{};
  uint16_t port = 0u;

  bool operator==(const Endpoint &) const = default;
};

class RendezvousTable {
public:
  static constexpr double kResendSeconds = 0.25;
  /* Shorter than the host's 15 s wait for a new player, so a pairing that
     fails is reported to the game while the host still waits for it. */
  static constexpr double kPartnerSeconds = 10.0;

  /* Start pairing `cookie` as side `index`. A second begin for the same
     cookie replaces the first, as a retried negotiation does. */
  void begin(uint32_t cookie, uint8_t index, const Endpoint &self, double now);

  /* The game gave up on `cookie`: nothing more is sent or reported for it. */
  void cancel(uint32_t cookie);

  /* Another instance's message. Its partner is the side with the same cookie
     and the other index; anything else is not ours and is ignored. */
  void heard(const Rendezvous &message);

  /* The messages to broadcast now. */
  std::vector<Rendezvous> due(double now);

  struct Outcome {
    uint32_t cookie = 0u;
    /* None when no partner answered within kPartnerSeconds. */
    std::optional<Endpoint> partner;
  };

  /* Each pairing's end, reported once: a partner heard, or none in time. A
     side that found its partner keeps answering until the partner says it
     heard too and has been told the same, or its time runs out. */
  std::vector<Outcome> take_outcomes(double now);

  bool empty() const { return pairings_.empty(); }

private:
  struct Pairing {
    uint8_t index = 0u;
    Endpoint self;
    double deadline = 0.0;
    double next_send = 0.0;
    std::optional<Endpoint> partner;
    bool reported = false;
    bool partner_heard_us = false;
    /* A message saying we heard the partner has gone out. */
    bool told_partner = false;
  };

  std::map<uint32_t, Pairing> pairings_;
};

} // namespace x2::net::presence

#endif
