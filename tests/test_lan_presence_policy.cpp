/* What a machine announces on the LAN, and how long it is remembered. */
#include "../src/net/lan_presence_policy.hpp"

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

presence::LocalFacts playing(const char *map) {
  presence::LocalFacts facts;
  facts.map = map;
  return facts;
}

std::optional<presence::Phase> phase_of(const presence::LocalFacts &facts) {
  const auto announce = presence::local_announce(facts, "pc");
  if (!announce) {
    return std::nullopt;
  }
  return announce->phase;
}

} // namespace

int main() {
  using presence::Phase;
  check("an act map is a campaign",
        presence::is_campaign_map("act2/savage/savage1"));
  check("case does not matter", presence::is_campaign_map("Act1/Hq/hq1"));
  check("a briefing is a campaign",
        presence::is_campaign_map("briefing/briefing3_10"));
  check("the front end is not", !presence::is_campaign_map("menu/main_back"));
  check("the boot map is not", !presence::is_campaign_map("main"));
  check("the Danger Room is not", !presence::is_campaign_map("dr/dr1"));
  check("a bonus map is not", !presence::is_campaign_map("bonus/b1"));
  check("no map is not", !presence::is_campaign_map(""));

  check("single player announces Playing",
        phase_of(playing("act1/a/a1")) == Phase::Playing);
  check("the front end announces nothing",
        !phase_of(playing("menu/main_back")));
  auto facts = playing("act1/a/a1");
  facts.directing = true;
  check("a re-form in progress announces nothing", !phase_of(facts));
  facts.hosting_lobby = true;
  facts.map = "menu/main_back";
  facts.session_players = 2u;
  const auto lobby = presence::local_announce(facts, "pc");
  check("a waiting lobby announces Lobby with its players",
        lobby && lobby->phase == Phase::Lobby && lobby->players == 2u);
  facts = playing("act1/a/a1");
  facts.session_players = 3u;
  check("a hosted game announces NetworkGame",
        phase_of(facts) == Phase::NetworkGame);
  facts.client = true;
  check("a client announces nothing", !phase_of(facts));

  presence::PeerTable table;
  const presence::Announce first{Phase::Playing, 1u, "one"};
  check("a new peer changes the list", table.heard(1u, first, 0.0));
  check("the same announce does not", !table.heard(1u, first, 1.0));
  check("a second peer changes it",
        table.heard(2u, {Phase::Lobby, 2u, "two"}, 1.5));
  check("peers stay in the order first heard",
        table.peers().size() == 2u && table.peers()[0].instance == 1u);
  check("a phase change changes it",
        table.heard(1u, {Phase::Lobby, 2u, "one"}, 2.0));
  check("a peer within its expiry stays",
        !table.expire(1.5 + presence::PeerTable::kExpirySeconds));
  check("a silent peer expires",
        table.expire(1.6 + presence::PeerTable::kExpirySeconds) &&
            table.peers().size() == 1u && table.find(1u) && !table.find(2u));

  if (failures) {
    std::printf("%d failure(s)\n", failures);
    return 1;
  }
  std::printf("lan presence policy: ok\n");
  return 0;
}
