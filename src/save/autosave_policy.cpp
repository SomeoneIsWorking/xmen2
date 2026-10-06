#include "autosave_policy.h"

namespace x2::save {

/* The title policy owns an asynchronous request -> write -> completion state
   machine. Its verified checkpoint is a successful retail map load, cancelled
   by main-menu Show, taken once the player controls a character. */

void autosave_policy_init(AutosavePolicy *policy) {
  if (policy)
    *policy = AutosavePolicy{};
}

void autosave_policy_map_return(AutosavePolicy *policy, int succeeded) {
  if (!policy)
    return;
  policy->map_returns++;
  if (!succeeded)
    return;
  policy->successful_map_returns++;
  policy->pending.id = policy->successful_map_returns;
  policy->pending.kind = AutosaveCheckpointKind::MapLoad;
  policy->has_pending = 1;
  policy->idle_polls = 0;
  policy->scheduled++;
}

void autosave_policy_menu_show(AutosavePolicy *policy) {
  if (!policy || !policy->has_pending)
    return;
  policy->has_pending = 0;
  policy->idle_polls = 0;
  policy->cancelled_menu++;
}

AutosavePollResult autosave_policy_poll(AutosavePolicy *policy,
                                        std::uint32_t manager_mode,
                                        int player_controls,
                                        AutosaveCheckpoint *checkpoint) {
  if (!policy)
    return AutosavePollResult::Idle;
  if (policy->has_active)
    return AutosavePollResult::AwaitingResult;
  if (!policy->has_pending)
    return AutosavePollResult::Idle;
  if (manager_mode != 0u) {
    policy->idle_polls = 0;
    policy->deferred_polls++;
    return AutosavePollResult::Deferred;
  }
  if (!player_controls) {
    policy->idle_polls = 0;
    policy->deferred_polls++;
    policy->control_deferred_polls++;
    return AutosavePollResult::Deferred;
  }
  policy->idle_polls++;
  if (policy->idle_polls < kAutosaveIdlePolls) {
    policy->deferred_polls++;
    return AutosavePollResult::Deferred;
  }
  policy->active = policy->pending;
  policy->has_active = 1;
  policy->has_pending = 0;
  policy->attempts++;
  if (checkpoint)
    *checkpoint = policy->active;
  return AutosavePollResult::Fire;
}

int autosave_policy_finish(AutosavePolicy *policy, std::uint64_t checkpoint_id,
                           int succeeded) {
  if (!policy || !policy->has_active || policy->active.id != checkpoint_id)
    return 0;
  policy->has_active = 0;
  if (succeeded)
    policy->successes++;
  else
    policy->failures++;
  return 1;
}

} // namespace x2::save
