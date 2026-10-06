#include "lan_nat_negotiation.hpp"

#include "guest_call.hpp"
#include "lan_coordinator.hpp"

extern "C" {
#include "winsock_posix.h"
#include "winsock_resolve.h"
#include "x2_log.h"
}

#include "x86rt_native.h"

#include <arpa/inet.h>
#include <cstring>
#include <netinet/in.h>
#include <optional>

namespace x2::lan {
namespace {

inline constexpr uint32_t kBeginNegotiationWithSocket = 0x0063b970u;
inline constexpr uint32_t kCancel = 0x0063ba10u;
/* NegotiateError and NegotiateResult, as GameSpy's natneg.h numbers them. */
inline constexpr uint32_t kNoError = 0u;
inline constexpr uint32_t kSocketError = 2u;
inline constexpr uint32_t kSuccess = 0u;
inline constexpr uint32_t kDeadbeatPartner = 1u;
inline constexpr uint32_t kSockaddrBytes = 16u;

using net::presence::Endpoint;

/* Where a peer on the LAN reaches `socket`: its port, and the address the
   game advertises for this machine when it is bound to every adapter. */
std::optional<Endpoint> endpoint_of(uint32_t socket) {
  sockaddr_in bound{};
  uint32_t error = 0u;
  if (!winsock_getsockname(socket, &bound, &error)) {
    x2_log_error("lan natneg: socket %u has no name (WSA error %u)", socket,
                 error);
    return std::nullopt;
  }
  uint32_t address = bound.sin_addr.s_addr;
  if (address == htonl(INADDR_ANY) &&
      winsock_local_addresses(&address, 1u) == 0u) {
    x2_log_error("lan natneg: this machine has no LAN address to offer");
    return std::nullopt;
  }
  Endpoint endpoint;
  std::memcpy(endpoint.address.data(), &address, sizeof address);
  endpoint.port = ntohs(bound.sin_port);
  return endpoint;
}

} // namespace

bool NatNegotiation::begin(uint32_t socket, uint32_t cookie, uint32_t index,
                           uint32_t completed, uint32_t userdata) {
  const auto self = endpoint_of(socket);
  if (!self) {
    return false;
  }
  x2_log_info("lan natneg: cookie %08x, side %u, offering %u.%u.%u.%u:%u",
              cookie, index, self->address[0], self->address[1],
              self->address[2], self->address[3], self->port);
  std::lock_guard lock(mutex_);
  game_[cookie] = {socket, completed, userdata};
  begun_.push_back({cookie, static_cast<uint8_t>(index), *self});
  return true;
}

void NatNegotiation::cancel(uint32_t cookie) {
  std::lock_guard lock(mutex_);
  game_.erase(cookie);
  std::erase_if(
      begun_, [cookie](const Begun &begun) { return begun.cookie == cookie; });
  table_.cancel(cookie);
}

void NatNegotiation::poll(const CPU &cpu, double now,
                          const std::vector<net::presence::Rendezvous> &heard,
                          net::presence::Presence &presence) {
  std::vector<std::pair<GameSide, net::presence::RendezvousTable::Outcome>>
      ended;
  {
    std::lock_guard lock(mutex_);
    for (const Begun &begun : begun_) {
      table_.begin(begun.cookie, begun.index, begun.self, now);
    }
    begun_.clear();
    for (const net::presence::Rendezvous &message : heard) {
      table_.heard(message);
    }
    for (const net::presence::Rendezvous &message : table_.due(now)) {
      presence.send_rendezvous(message);
    }
    for (const auto &outcome : table_.take_outcomes(now)) {
      const auto side = game_.find(outcome.cookie);
      if (side != game_.end()) {
        ended.emplace_back(side->second, outcome);
        game_.erase(side);
      }
    }
  }
  /* The callback runs guest code that may begin or cancel another pairing,
     so it is answered outside the lock. */
  for (const auto &[side, outcome] : ended) {
    complete(cpu, side, outcome);
  }
}

void NatNegotiation::complete(
    const CPU &cpu, const GameSide &side,
    const net::presence::RendezvousTable::Outcome &outcome) {
  guest::GuestBlock sockaddr(kSockaddrBytes);
  if (!sockaddr) {
    x2_log_error("lan natneg: no guest memory to answer cookie %08x",
                 outcome.cookie);
    return;
  }
  sockaddr_in partner{};
  partner.sin_family = AF_INET;
  if (outcome.partner) {
    std::memcpy(&partner.sin_addr.s_addr, outcome.partner->address.data(),
                sizeof partner.sin_addr.s_addr);
    partner.sin_port = htons(outcome.partner->port);
    x2_log_info("lan natneg: cookie %08x paired with %u.%u.%u.%u:%u",
                outcome.cookie, outcome.partner->address[0],
                outcome.partner->address[1], outcome.partner->address[2],
                outcome.partner->address[3], outcome.partner->port);
  } else {
    x2_log_info("lan natneg: cookie %08x: no partner answered within %.0f s",
                outcome.cookie,
                net::presence::RendezvousTable::kPartnerSeconds);
  }
  winsock_sockaddr_from_host(&partner, sockaddr.bytes());
  guest::GuestCall(cpu).cdecl_call(
      side.completed, {outcome.partner ? kSuccess : kDeadbeatPartner,
                       side.socket, sockaddr.address(), side.userdata});
}

namespace {

/* Argument `index` of a cdecl call, as the callee sees its stack. */
uint32_t argument(const CPU *C, uint32_t index) {
  return RD32(C->reg[kX86pEsp] + 4u + index * 4u);
}

/* NegotiateError NNBeginNegotiationWithSocket(SOCKET, int cookie,
   int clientindex, progress, completed, void *userdata) -- cdecl. */
void begin_negotiation(CPU *C) {
  const bool begun = coordinator().nat_negotiation().begin(
      argument(C, 0u), argument(C, 1u), argument(C, 2u), argument(C, 4u),
      argument(C, 5u));
  C->reg[kX86pEax] = begun ? kNoError : kSocketError;
  C->reg[kX86pEsp] += 4u;
}

/* void NNCancel(int cookie) -- cdecl. */
void cancel_negotiation(CPU *C) {
  coordinator().nat_negotiation().cancel(argument(C, 0u));
  C->reg[kX86pEsp] += 4u;
}

__attribute__((constructor)) void register_nat_negotiation() {
  x86_register_override("XMen2.exe", kBeginNegotiationWithSocket,
                        begin_negotiation);
  x86_register_override("XMen2.exe", kCancel, cancel_negotiation);
}

} // namespace
} // namespace x2::lan
