#include "player_participation_policy.h"

#include <string.h>

#define PLAYER_MASK ((1u << X2_PARTICIPATION_PLAYERS) - 1u)

void x2_player_participation_policy_init(X2PlayerParticipationPolicy *policy) {
  if (policy)
    memset(policy, 0, sizeof *policy);
}

void x2_player_participation_policy_configure(
    X2PlayerParticipationPolicy *policy, uint8_t eligible_players) {
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

void x2_player_participation_policy_note_start(
    X2PlayerParticipationPolicy *policy, unsigned player, int down) {
  uint8_t bit;

  if (!policy || player >= X2_PARTICIPATION_PLAYERS)
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

X2PlayerParticipationTransition
x2_player_participation_policy_consume(X2PlayerParticipationPolicy *policy) {
  X2PlayerParticipationTransition out = {0, 0};

  if (!policy)
    return out;
  out.join = policy->pending_join & policy->eligible;
  out.leave = policy->pending_leave & (uint8_t)~policy->eligible;
  out.join &= (uint8_t)~out.leave;
  policy->pending_join = 0;
  policy->pending_leave = 0;
  return out;
}
