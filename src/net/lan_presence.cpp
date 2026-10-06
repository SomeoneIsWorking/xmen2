#include "lan_presence.hpp"

#include <algorithm>
#include <array>

namespace x2::net::presence {
namespace {

/* Enough for any backlog one poll can meet, without starving the game. */
inline constexpr int kDatagramsPerPoll = 64;

} // namespace

bool Presence::start(uint16_t port) { return socket_.open(port); }

bool Presence::send(const Body &body) {
  return socket_.broadcast(encode({instance_, body}));
}

void Presence::receive(double now, Heard &heard) {
  std::array<uint8_t, kMaxDatagramBytes> buffer{};
  for (int count = 0; count < kDatagramsPerPoll; count++) {
    const auto size = socket_.receive(buffer);
    if (!size) {
      return;
    }
    if (*size > buffer.size()) {
      continue;
    }
    const auto message = decode(std::span<const uint8_t>(buffer.data(), *size));
    if (!message || message->sender == instance_) {
      continue;
    }
    if (const auto *announce = std::get_if<Announce>(&message->body)) {
      heard.peers_changed |= peers_.heard(message->sender, *announce, now);
    } else if (const auto *join = std::get_if<JoinRequest>(&message->body)) {
      if (join->host == instance_ &&
          std::find(heard.join_requesters.begin(), heard.join_requesters.end(),
                    message->sender) == heard.join_requesters.end()) {
        heard.join_requesters.push_back(message->sender);
      }
    } else {
      heard.rendezvous.push_back(std::get<Rendezvous>(message->body));
    }
  }
}

Presence::Heard Presence::poll(double now,
                               const std::optional<Announce> &local) {
  Heard heard;
  if (!socket_.is_open()) {
    return heard;
  }
  receive(now, heard);
  heard.peers_changed |= peers_.expire(now);
  if (local && (local != announced_ || now >= next_announce_)) {
    next_announce_ = now + kAnnounceSeconds;
    send(*local);
  }
  announced_ = local;
  return heard;
}

bool Presence::request_join(uint64_t host) {
  return socket_.is_open() && send(JoinRequest{host});
}

bool Presence::send_rendezvous(const Rendezvous &message) {
  return socket_.is_open() && send(message);
}

} // namespace x2::net::presence
