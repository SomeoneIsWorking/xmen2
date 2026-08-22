#ifndef X2_NATIVE_PLAYER_PARTICIPATION_H
#define X2_NATIVE_PLAYER_PARTICIPATION_H

#include <stdint.h>

struct CPU;

/* Apply policy decisions through the retail participation manager. This is
   deliberately an API call bridge: the pause-menu Players screen reads the
   same owner, and no host code writes its active flags or count directly. */
void x2_player_participation_apply(struct CPU *cpu, uint8_t join_players,
                                   uint8_t leave_players);

/* Reconcile retail state that may have changed outside the host policy (for
   example through the pause Players page). Active players without an assigned
   source are removed through the same retail leave/reconcile API. */
void x2_player_participation_enforce_eligibility(struct CPU *cpu,
                                                 uint8_t eligible_players);

#endif
