#ifndef X2_LAN_PRESENCE_PROTOCOL_HPP
#define X2_LAN_PRESENCE_PROTOCOL_HPP

/*
 * The port's LAN presence datagrams (issue #188).
 *
 * XMen2.exe only advertises a game from inside its online lobby, so a game
 * someone is simply playing is invisible on the network. Every port instance
 * that could take a player announces itself on its own UDP port, and a
 * player who picks one from the main menu asks it to open its lobby. The
 * game's own session, transfer, and join traffic is untouched: presence only
 * decides who re-forms and who joins.
 *
 *   header   "X2LN", version, kind, sender instance (u64, little endian)
 *   Announce phase, players, name length, name (at most kMaxNameBytes)
 *   JoinRequest  the instance asked to host (u64)
 *   Rendezvous   cookie (u32), client index, IPv4 octets, port (u16),
 *                partner heard (0 or 1)
 *
 * decode() refuses anything it cannot read completely -- another program's
 * datagram, another version, a truncation, trailing bytes -- rather than
 * guessing at it.
 */

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace x2::net::presence {

inline constexpr uint16_t kPort = 5166u;
inline constexpr uint8_t kVersion = 1u;
inline constexpr size_t kMaxNameBytes = 32u;
inline constexpr size_t kMaxDatagramBytes = 64u;

enum class Phase : uint8_t {
  /* A single-player campaign: joining re-forms it as a hosted campaign. */
  Playing = 1,
  /* A LAN lobby waiting for players: join it directly. */
  Lobby = 2,
  /* A hosted LAN game in progress: joining re-forms it with everyone. */
  NetworkGame = 3
};

struct Announce {
  Phase phase = Phase::Playing;
  uint8_t players = 1u;
  std::string name;

  bool operator==(const Announce &) const = default;
};

struct JoinRequest {
  uint64_t host = 0u;

  bool operator==(const JoinRequest &) const = default;
};

/* One side of a pairing the game asked GameSpy's NAT negotiation for: the
   two sides share `cookie`, differ in `index`, and each names the endpoint of
   its own game socket. `partner_heard` tells the other side it may stop. */
struct Rendezvous {
  uint32_t cookie = 0u;
  uint8_t index = 0u;
  std::array<uint8_t, 4> address{};
  uint16_t port = 0u;
  bool partner_heard = false;

  bool operator==(const Rendezvous &) const = default;
};

using Body = std::variant<Announce, JoinRequest, Rendezvous>;

struct Message {
  uint64_t sender = 0u;
  Body body;

  bool operator==(const Message &) const = default;
};

/* An Announce name longer than kMaxNameBytes is cut at a UTF-8 boundary. */
std::vector<uint8_t> encode(const Message &message);

std::optional<Message> decode(std::span<const uint8_t> datagram);

} // namespace x2::net::presence

#endif
