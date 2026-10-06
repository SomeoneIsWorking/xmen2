/* The LAN presence datagrams: what goes out comes back, and nothing else
   is read. */
#include "../src/net/lan_presence_protocol.hpp"

#include <cstdio>

namespace {

namespace presence = x2::net::presence;

int failures = 0;

void check(const char *what, bool ok) {
  if (!ok) {
    std::printf("FAIL: %s\n", what);
    failures++;
  }
}

bool round_trips(const presence::Message &message) {
  const auto bytes = presence::encode(message);
  const auto back = presence::decode(bytes);
  return bytes.size() <= presence::kMaxDatagramBytes && back &&
         *back == message;
}

} // namespace

int main() {
  const presence::Message announce{
      0x0123456789abcdefULL,
      presence::Announce{presence::Phase::NetworkGame, 3u, "den-pc"}};
  const presence::Message join{42u, presence::JoinRequest{0xfeedULL}};
  check("an Announce round-trips", round_trips(announce));
  check("a JoinRequest round-trips", round_trips(join));
  check("an empty name round-trips",
        round_trips({7u, presence::Announce{presence::Phase::Lobby, 1u, ""}}));

  const std::string long_name(40, 'x');
  auto cut = presence::decode(presence::encode(
      {1u, presence::Announce{presence::Phase::Playing, 1u, long_name}}));
  check("a long name is cut to the limit",
        cut && std::get<presence::Announce>(cut->body).name.size() ==
                   presence::kMaxNameBytes);
  /* 31 ASCII bytes, then a two-byte character straddling the limit. */
  const std::string straddling = std::string(31, 'a') + "\xc3\xa9" + "z";
  cut = presence::decode(presence::encode(
      {1u, presence::Announce{presence::Phase::Playing, 1u, straddling}}));
  check("a cut never splits a character",
        cut && std::get<presence::Announce>(cut->body).name ==
                   std::string(31, 'a'));

  const auto good = presence::encode(announce);
  for (size_t length = 0; length < good.size(); length++) {
    std::vector<uint8_t> truncated(good.begin(),
                                   good.begin() + static_cast<long>(length));
    if (presence::decode(truncated)) {
      std::printf("FAIL: a %zu-byte truncation decoded\n", length);
      failures++;
    }
  }
  auto trailing = good;
  trailing.push_back(0u);
  check("trailing bytes are refused", !presence::decode(trailing));
  auto magic = good;
  magic[0] = 'Y';
  check("another program's datagram is refused", !presence::decode(magic));
  auto version = good;
  version[4] = presence::kVersion + 1u;
  check("another version is refused", !presence::decode(version));
  auto kind = good;
  kind[5] = 9u;
  check("an unknown kind is refused", !presence::decode(kind));
  auto phase = good;
  phase[14] = 0u;
  check("an unknown phase is refused", !presence::decode(phase));
  /* A complete datagram whose name is one byte over the limit. */
  auto length = presence::encode(
      {1u, presence::Announce{presence::Phase::Playing, 1u, ""}});
  length[16] = presence::kMaxNameBytes + 1u;
  length.insert(length.end(), presence::kMaxNameBytes + 1u, 'x');
  check("an overlong name is refused", !presence::decode(length));

  const presence::Message rendezvous{
      9u, presence::Rendezvous{
              0xa1b2c3d4u, 1u, {192u, 168u, 1u, 107u}, 52757u, true}};
  check("a Rendezvous round-trips", round_trips(rendezvous));
  const auto pairing = presence::encode(rendezvous);
  for (size_t cut_at = 0; cut_at < pairing.size(); cut_at++) {
    std::vector<uint8_t> truncated(pairing.begin(),
                                   pairing.begin() + static_cast<long>(cut_at));
    if (presence::decode(truncated)) {
      std::printf("FAIL: a %zu-byte Rendezvous truncation decoded\n", cut_at);
      failures++;
    }
  }
  /* The partner-heard flag is the last byte and only 0 or 1 is read. */
  auto flag = pairing;
  flag.back() = 2u;
  check("a partner-heard flag other than 0 or 1 is refused",
        !presence::decode(flag));

  if (failures) {
    std::printf("%d failure(s)\n", failures);
    return 1;
  }
  std::printf("lan presence protocol: ok\n");
  return 0;
}
