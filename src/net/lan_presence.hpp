#ifndef X2_LAN_PRESENCE_HPP
#define X2_LAN_PRESENCE_HPP

/*
 * This machine on the LAN: announce what it offers, hear what others offer,
 * and carry join requests between them (issue #188).
 *
 * Every presence datagram is broadcast, so a request reaches its host by the
 * instance id inside it, never by an address -- two instances on one machine
 * are told apart exactly as two machines are.
 */

#include "lan_presence_policy.hpp"
#include "udp_broadcast_socket.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace x2::net::presence {

class Presence {
public:
  /* A silent instance is forgotten after three missed announcements. */
  static constexpr double kAnnounceSeconds = 1.0;

  explicit Presence(uint64_t instance) : instance_(instance) {}

  /* Open the socket. On failure error() says why; nothing else works. */
  bool start(uint16_t port = kPort);
  const std::string &error() const { return socket_.error(); }

  struct Heard {
    bool peers_changed = false;
    /* The instances that asked this one to open its lobby, each once. */
    std::vector<uint64_t> join_requesters;
    /* Other instances' NAT-negotiation stand-in messages, as they came. */
    std::vector<Rendezvous> rendezvous;
  };

  /* Drain what arrived, forget the silent, and announce `local` when one is
     due (at once when it changed). No announcement while `local` is none. */
  Heard poll(double now, const std::optional<Announce> &local);

  /* Ask `host` to open its lobby. False when the send failed. */
  bool request_join(uint64_t host);

  /* Broadcast one side of a pairing. False when the send failed. */
  bool send_rendezvous(const Rendezvous &message);

  const PeerTable &peers() const { return peers_; }
  uint64_t instance() const { return instance_; }

private:
  void receive(double now, Heard &heard);
  bool send(const Body &body);

  uint64_t instance_;
  BroadcastSocket socket_;
  PeerTable peers_;
  std::optional<Announce> announced_;
  double next_announce_ = 0.0;
};

} // namespace x2::net::presence

#endif
