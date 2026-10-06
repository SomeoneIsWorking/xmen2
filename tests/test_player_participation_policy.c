#include "player_participation_policy.h"

#include <assert.h>
#include <stdio.h>

static int checks;
#define CHECK(c)                                                               \
  do {                                                                         \
    assert(c);                                                                 \
    checks++;                                                                  \
  } while (0)

int main(void) {
  X2PlayerParticipationPolicy policy;
  X2PlayerParticipationTransition transition;

  x2_player_participation_policy_init(&policy);
  x2_player_participation_policy_configure(&policy, 0x05u);
  transition = x2_player_participation_policy_consume(&policy);
  CHECK(transition.join == 0x01u);
  CHECK(transition.leave == 0x0au);

  /* Eligibility alone never joins P2-P4. */
  x2_player_participation_policy_configure(&policy, 0x0fu);
  transition = x2_player_participation_policy_consume(&policy);
  CHECK(transition.join == 0u);
  CHECK(transition.leave == 0u);

  /* Only a rising Start edge joins that eligible player. */
  x2_player_participation_policy_note_start(&policy, 2u, 1);
  transition = x2_player_participation_policy_consume(&policy);
  CHECK(transition.join == 0x04u);
  x2_player_participation_policy_note_start(&policy, 2u, 1);
  CHECK(x2_player_participation_policy_consume(&policy).join == 0u);
  x2_player_participation_policy_note_start(&policy, 2u, 0);
  x2_player_participation_policy_note_start(&policy, 2u, 1);
  CHECK(x2_player_participation_policy_consume(&policy).join == 0x04u);

  /* Losing the last assignment forces leave and cancels a queued join. */
  x2_player_participation_policy_note_start(&policy, 3u, 1);
  x2_player_participation_policy_configure(&policy, 0x07u);
  transition = x2_player_participation_policy_consume(&policy);
  CHECK(transition.join == 0u);
  CHECK(transition.leave == 0x08u);

  /* P1 becomes default-active each time it becomes eligible, but Start is
     never a second-player join path for P1. */
  x2_player_participation_policy_configure(&policy, 0x06u);
  transition = x2_player_participation_policy_consume(&policy);
  CHECK(transition.leave == 0x09u);
  x2_player_participation_policy_configure(&policy, 0x07u);
  CHECK(x2_player_participation_policy_consume(&policy).join == 0x01u);
  x2_player_participation_policy_note_start(&policy, 0u, 1);
  CHECK(x2_player_participation_policy_consume(&policy).join == 0u);

  /* Seats map to game players through the game's own table. The three maps
     are the ones observed live: local play, a LAN host whose client plays
     game player 1 on a controller flagged remote, and that LAN client, whose
     keyboard (seat 0) drives game player 1 behind the host's player 0. */
  {
    const X2PlayerSeatMap local = {{0, 1, 2, 3}, 0x0fu};
    const X2PlayerSeatMap host = {{0, 1, 2, 3}, 0x0du};
    const X2PlayerSeatMap client = {{1, 0, 2, 3}, 0x0fu};
    const X2PlayerSeatMap unmapped = {{-1, 7, 2, 3}, 0x0fu};

    CHECK(x2_player_seats_to_players(&local, 0x05u) == 0x05u);
    CHECK(x2_player_seats_governed(&local) == 0x0fu);

    /* The host never governs the network player, eligible seat or not. */
    CHECK(x2_player_seats_governed(&host) == 0x0du);
    CHECK(x2_player_seats_to_players(&host, 0x0fu) == 0x0du);

    /* The client's seat 0 is game player 1, not game player 0. */
    CHECK(x2_player_seats_to_players(&client, 0x01u) == 0x02u);
    CHECK(x2_player_seats_to_players(&client, 0x02u) == 0x01u);

    /* A player with no controller, or one out of range, is nobody's seat. */
    CHECK(x2_player_seats_governed(&unmapped) == 0x0cu);
    CHECK(x2_player_seats_to_players(NULL, 0x0fu) == 0u);
  }

  printf("player_participation_policy: %d checks passed\n", checks);
  return 0;
}
