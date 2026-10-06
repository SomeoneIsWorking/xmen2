/* Pairing two game sockets through broadcast messages: both sides learn the
   other's endpoint once each, even when one starts late, and a side with no
   partner is told so in time. */
#include "../src/net/lan_rendezvous.hpp"

#include <cstdio>

namespace {

namespace presence = x2::net::presence;
using Table = presence::RendezvousTable;

int failures = 0;

void check(const char *what, bool ok) {
  if (!ok) {
    std::printf("FAIL: %s\n", what);
    failures++;
  }
}

const presence::Endpoint kNewcomer{{192u, 168u, 1u, 107u}, 52757u};
const presence::Endpoint kClient{{192u, 168u, 1u, 107u}, 41334u};

/* Deliver everything `from` has due to `to`, as a broadcast would. */
void deliver(Table &from, Table &to, double now) {
  for (const presence::Rendezvous &message : from.due(now)) {
    to.heard(message);
  }
}

} // namespace

int main() {
  /* The client already in the game starts first; the newcomer's message
     from the host arrives a second later. */
  {
    Table client;
    Table newcomer;
    client.begin(77u, 0u, kClient, 0.0);
    deliver(client, newcomer, 0.0);
    check("a pairing nobody else began is not reported",
          newcomer.take_outcomes(0.0).empty());
    newcomer.begin(77u, 1u, kNewcomer, 1.0);
    deliver(newcomer, client, 1.0);
    const auto client_end = client.take_outcomes(1.0);
    check("the early side learns the late side's endpoint",
          client_end.size() == 1u && client_end[0].cookie == 77u &&
              client_end[0].partner == kNewcomer);
    /* The early side answers at once, saying it heard. */
    deliver(client, newcomer, 1.0);
    const auto newcomer_end = newcomer.take_outcomes(1.0);
    check("the late side learns the early side's endpoint",
          newcomer_end.size() == 1u && newcomer_end[0].partner == kClient);
    check("a side stays until it has told its partner", !newcomer.empty());
    /* The late side's one message saying it heard. */
    deliver(newcomer, client, 1.0);
    check("a side is done once both have said they heard",
          newcomer.take_outcomes(1.0).empty() && newcomer.empty());
    check("each pairing is reported once", client.take_outcomes(1.0).empty());
    check("both sides stop once each has heard the other", client.empty());
    check("nothing more is sent",
          client.due(2.0).empty() && newcomer.due(2.0).empty());
  }
  /* Nobody answers. */
  {
    Table lonely;
    lonely.begin(5u, 1u, kNewcomer, 0.0);
    check("a pairing waits while there is time",
          lonely.take_outcomes(Table::kPartnerSeconds - 0.5).empty());
    const auto end = lonely.take_outcomes(Table::kPartnerSeconds);
    check("a pairing with no partner ends without one",
          end.size() == 1u && end[0].cookie == 5u && !end[0].partner);
    check("an ended pairing is forgotten", lonely.empty());
  }
  /* Messages that are not this side's partner. */
  {
    Table side;
    side.begin(9u, 0u, kClient, 0.0);
    side.heard({9u, 0u, kNewcomer.address, kNewcomer.port, false});
    side.heard({10u, 1u, kNewcomer.address, kNewcomer.port, false});
    check("the same index or another cookie is not a partner",
          side.take_outcomes(0.5).empty());
    side.cancel(9u);
    check("a cancelled pairing is forgotten", side.empty());
    side.heard({9u, 1u, kNewcomer.address, kNewcomer.port, false});
    check("a cancelled pairing reports nothing",
          side.take_outcomes(0.5).empty());
  }
  /* The resend cadence. */
  {
    Table side;
    side.begin(3u, 0u, kClient, 0.0);
    check("a new pairing is due at once", side.due(0.0).size() == 1u);
    check("nothing is due before the resend interval",
          side.due(Table::kResendSeconds / 2.0).empty());
    check("it is due again after the interval",
          side.due(Table::kResendSeconds).size() == 1u);
  }

  if (failures) {
    std::printf("%d failure(s)\n", failures);
    return 1;
  }
  std::printf("lan rendezvous: ok\n");
  return 0;
}
