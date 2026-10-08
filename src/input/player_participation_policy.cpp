#include "player_participation_policy.h"

#include <string.h>

#define PLAYER_MASK ((1u << kParticipationPlayers) - 1u)

namespace x2::input {

void player_participation_policy_init(PlayerParticipationPolicy *policy) {
  if (policy)
    memset(policy, 0, sizeof *policy);
}

void player_participation_policy_configure(PlayerParticipationPolicy *policy,
                                           uint8_t eligible_players) {
  uint8_t eligible, changed;

  if (!policy)
    return;
  eligible = eligible_players & PLAYER_MASK;
  changed = policy->eligible ^ eligible;
  policy->start_down &= (uint8_t)~changed;
  policy->pending_join &= eligible;
  if (!policy->configured || changed) {
    policy->pending_leave |= (uint8_t)(~eligible & PLAYER_MASK);
    if ((eligible & 1u) && (!policy->configured || !(policy->eligible & 1u)))
      policy->pending_join |= 1u;
  }
  policy->eligible = eligible;
  policy->configured = 1u;
}

void player_participation_policy_note_start(PlayerParticipationPolicy *policy,
                                            unsigned player, int down) {
  uint8_t bit;

  if (!policy || player >= kParticipationPlayers)
    return;
  bit = (uint8_t)(1u << player);
  if (down) {
    if (player > 0u && (policy->eligible & bit) && !(policy->start_down & bit))
      policy->pending_join |= bit;
    policy->start_down |= bit;
  } else {
    policy->start_down &= (uint8_t)~bit;
  }
}

PlayerParticipationTransition
player_participation_policy_consume(PlayerParticipationPolicy *policy) {
  PlayerParticipationTransition out = {0, 0};

  if (!policy)
    return out;
  out.join = policy->pending_join & policy->eligible;
  out.leave = policy->pending_leave & (uint8_t)~policy->eligible;
  out.join &= (uint8_t)~out.leave;
  policy->pending_join = 0;
  policy->pending_leave = 0;
  return out;
}

uint8_t player_seats_to_players(const PlayerSeatMap *map, uint8_t seats) {
  uint8_t players = 0;
  unsigned player;

  if (!map)
    return 0;
  for (player = 0; player < kParticipationPlayers; player++) {
    const int32_t controller = map->controller_of_player[player];
    if (controller >= 0 && controller < (int32_t)kParticipationPlayers &&
        (map->local_controllers & seats & (1u << controller)))
      players |= (uint8_t)(1u << player);
  }
  return players;
}

uint8_t player_seats_governed(const PlayerSeatMap *map) {
  return player_seats_to_players(map, PLAYER_MASK);
}

} // namespace x2::input
