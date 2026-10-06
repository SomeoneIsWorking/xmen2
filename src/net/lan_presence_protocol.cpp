#include "lan_presence_protocol.hpp"

#include <array>

namespace x2::net::presence {
namespace {

inline constexpr std::array<uint8_t, 4> kMagic = {'X', '2', 'L', 'N'};
inline constexpr uint8_t kAnnounceKind = 1u;
inline constexpr uint8_t kJoinRequestKind = 2u;
inline constexpr uint8_t kRendezvousKind = 3u;

void put_u16(std::vector<uint8_t> &out, uint16_t value) {
  out.push_back(static_cast<uint8_t>(value));
  out.push_back(static_cast<uint8_t>(value >> 8u));
}

void put_u32(std::vector<uint8_t> &out, uint32_t value) {
  for (unsigned shift = 0; shift < 32u; shift += 8u) {
    out.push_back(static_cast<uint8_t>(value >> shift));
  }
}

void put_u64(std::vector<uint8_t> &out, uint64_t value) {
  for (unsigned shift = 0; shift < 64u; shift += 8u) {
    out.push_back(static_cast<uint8_t>(value >> shift));
  }
}

/* The longest prefix of `name` that fits and ends on a character boundary. */
size_t fitted_length(const std::string &name) {
  if (name.size() <= kMaxNameBytes) {
    return name.size();
  }
  size_t length = kMaxNameBytes;
  while (length > 0u && (static_cast<uint8_t>(name[length]) & 0xc0u) == 0x80u) {
    length--;
  }
  return length;
}

class Reader {
public:
  explicit Reader(std::span<const uint8_t> bytes) : bytes_(bytes) {}

  std::optional<uint8_t> u8() {
    if (at_ >= bytes_.size()) {
      return std::nullopt;
    }
    return bytes_[at_++];
  }

  std::optional<uint16_t> u16() { return little_endian<uint16_t>(); }
  std::optional<uint32_t> u32() { return little_endian<uint32_t>(); }
  std::optional<uint64_t> u64() { return little_endian<uint64_t>(); }

  std::optional<std::string> text(size_t length) {
    if (bytes_.size() - at_ < length) {
      return std::nullopt;
    }
    std::string value(reinterpret_cast<const char *>(bytes_.data() + at_),
                      length);
    at_ += length;
    return value;
  }

  bool finished() const { return at_ == bytes_.size(); }

private:
  template <typename Value> std::optional<Value> little_endian() {
    if (bytes_.size() - at_ < sizeof(Value)) {
      return std::nullopt;
    }
    Value value = 0u;
    for (unsigned shift = 0; shift < sizeof(Value) * 8u; shift += 8u) {
      value = static_cast<Value>(value | static_cast<Value>(bytes_[at_++])
                                             << shift);
    }
    return value;
  }

  std::span<const uint8_t> bytes_;
  size_t at_ = 0u;
};

std::optional<Phase> phase_from(uint8_t value) {
  switch (value) {
  case static_cast<uint8_t>(Phase::Playing):
  case static_cast<uint8_t>(Phase::Lobby):
  case static_cast<uint8_t>(Phase::NetworkGame):
    return static_cast<Phase>(value);
  default:
    return std::nullopt;
  }
}

std::optional<Body> decode_announce(Reader &reader) {
  const auto phase = reader.u8();
  const auto players = reader.u8();
  const auto length = reader.u8();
  if (!phase || !players || !length || *length > kMaxNameBytes) {
    return std::nullopt;
  }
  const auto known = phase_from(*phase);
  auto name = reader.text(*length);
  if (!known || !name) {
    return std::nullopt;
  }
  return Announce{*known, *players, std::move(*name)};
}

std::optional<Body> decode_join_request(Reader &reader) {
  const auto host = reader.u64();
  if (!host) {
    return std::nullopt;
  }
  return JoinRequest{*host};
}

std::optional<Body> decode_rendezvous(Reader &reader) {
  Rendezvous rendezvous;
  const auto cookie = reader.u32();
  const auto index = reader.u8();
  if (!cookie || !index) {
    return std::nullopt;
  }
  for (uint8_t &octet : rendezvous.address) {
    const auto value = reader.u8();
    if (!value) {
      return std::nullopt;
    }
    octet = *value;
  }
  const auto port = reader.u16();
  const auto heard = reader.u8();
  if (!port || !heard || *heard > 1u) {
    return std::nullopt;
  }
  rendezvous.cookie = *cookie;
  rendezvous.index = *index;
  rendezvous.port = *port;
  rendezvous.partner_heard = *heard == 1u;
  return rendezvous;
}

} // namespace

std::vector<uint8_t> encode(const Message &message) {
  std::vector<uint8_t> out(kMagic.begin(), kMagic.end());
  out.push_back(kVersion);
  if (const auto *announce = std::get_if<Announce>(&message.body)) {
    out.push_back(kAnnounceKind);
    put_u64(out, message.sender);
    const size_t length = fitted_length(announce->name);
    out.push_back(static_cast<uint8_t>(announce->phase));
    out.push_back(announce->players);
    out.push_back(static_cast<uint8_t>(length));
    out.insert(out.end(), announce->name.begin(),
               announce->name.begin() + static_cast<ptrdiff_t>(length));
  } else if (const auto *join = std::get_if<JoinRequest>(&message.body)) {
    out.push_back(kJoinRequestKind);
    put_u64(out, message.sender);
    put_u64(out, join->host);
  } else {
    const auto &rendezvous = std::get<Rendezvous>(message.body);
    out.push_back(kRendezvousKind);
    put_u64(out, message.sender);
    put_u32(out, rendezvous.cookie);
    out.push_back(rendezvous.index);
    out.insert(out.end(), rendezvous.address.begin(), rendezvous.address.end());
    put_u16(out, rendezvous.port);
    out.push_back(rendezvous.partner_heard ? 1u : 0u);
  }
  return out;
}

std::optional<Message> decode(std::span<const uint8_t> datagram) {
  Reader reader(datagram);
  for (const uint8_t expected : kMagic) {
    if (reader.u8() != expected) {
      return std::nullopt;
    }
  }
  const auto version = reader.u8();
  const auto kind = reader.u8();
  const auto sender = reader.u64();
  if (version != kVersion || !kind || !sender) {
    return std::nullopt;
  }
  std::optional<Body> body;
  if (*kind == kAnnounceKind) {
    body = decode_announce(reader);
  } else if (*kind == kJoinRequestKind) {
    body = decode_join_request(reader);
  } else if (*kind == kRendezvousKind) {
    body = decode_rendezvous(reader);
  }
  if (!body || !reader.finished()) {
    return std::nullopt;
  }
  return Message{*sender, std::move(*body)};
}

} // namespace x2::net::presence
