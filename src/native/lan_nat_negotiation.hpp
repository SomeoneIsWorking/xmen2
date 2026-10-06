#ifndef X2_LAN_NAT_NEGOTIATION_HPP
#define X2_LAN_NAT_NEGOTIATION_HPP

/*
 * GameSpy's NAT negotiation, answered on the LAN (issue #188).
 *
 * A session of three or more links its clients to each other. The host sends
 * the newcomer message 0x21 and every client already in the game message
 * 0x22, each carrying one cookie (FUN_00605870), and both sides call
 * NNBeginNegotiationWithSocket (0x0063b970) so natneg*.gamespy.com can tell
 * each the other's address. Those names no longer resolve, so Begin fails at
 * once, the game ignores the failure, and the host gives up on the player
 * after 15 s ("Unable to add player 3").
 *
 * The port answers Begin itself. Both sides broadcast their game socket's
 * endpoint under the cookie on the presence channel (RendezvousTable), and
 * each completes through the callback the game gave -- 0x006050a0 with
 * nr_success and the partner's sockaddr_in -- which links the two exactly as
 * a GameSpy introduction would. A partner that never answers completes with
 * nr_deadbeatpartner, the result GameSpy gives for the same case.
 */

#include "lan_presence.hpp"
#include "lan_rendezvous.hpp"

extern "C" {
#include "x86rt.h"
}

#include <cstdint>
#include <map>
#include <mutex>
#include <vector>

namespace x2::lan {

class NatNegotiation {
public:
  /* The guest thread, from NNBeginNegotiationWithSocket: pair `cookie` as
     side `index`, and answer `completed(result, socket, sockaddr_in *,
     userdata)` when it ends. False when the socket has no endpoint. */
  bool begin(uint32_t socket, uint32_t cookie, uint32_t index,
             uint32_t completed, uint32_t userdata);

  /* The guest thread, from NNCancel. */
  void cancel(uint32_t cookie);

  /* The guest's input thread, once per poll: exchange messages and answer
     the game for every pairing that ended. */
  void poll(const CPU &cpu, double now,
            const std::vector<net::presence::Rendezvous> &heard,
            net::presence::Presence &presence);

private:
  struct GameSide {
    uint32_t socket = 0u;
    uint32_t completed = 0u;
    uint32_t userdata = 0u;
  };
  struct Begun {
    uint32_t cookie = 0u;
    uint8_t index = 0u;
    net::presence::Endpoint self;
  };

  void complete(const CPU &cpu, const GameSide &side,
                const net::presence::RendezvousTable::Outcome &outcome);

  std::mutex mutex_;
  /* Begun since the last poll, which owns the clock the table runs on. */
  std::vector<Begun> begun_;
  std::map<uint32_t, GameSide> game_;
  net::presence::RendezvousTable table_;
};

} // namespace x2::lan

#endif
