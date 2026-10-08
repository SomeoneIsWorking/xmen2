#pragma once

#include <cstdint>

struct X86pCpu;

namespace x2::native {

/* Apply policy decisions through the retail participation manager. This is
   deliberately an API call bridge: the pause-menu Players screen reads the
   same owner, and no host code writes its active flags or count directly.
   Both calls take LOCAL SEATS and act on the game players those seats drive
   (player_participation_policy.h); a network player is never touched. */
void player_participation_apply(X86pCpu *cpu, uint8_t join_seats,
                                uint8_t leave_seats);

/* Reconcile retail state that may have changed outside the host policy (for
   example through the pause Players page). Active players without an assigned
   source are removed through the same retail leave/reconcile API. */
void player_participation_enforce_eligibility(X86pCpu *cpu,
                                              uint8_t eligible_seats);

} // namespace x2::native
