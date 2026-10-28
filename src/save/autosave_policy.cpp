#include "autosave_policy.h"

namespace x2::save {

/* The title policy owns an asynchronous request -> write -> completion state
   machine. Its one checkpoint is a hero stepping onto an extraction pad, at
   most once per cooldown, cancelled by main-menu Show, taken once the player
   controls a character. */

void autosave_policy_init(AutosavePolicy *policy) {
  if (policy)
    *policy = AutosavePolicy{};
}

static void schedule(AutosavePolicy *policy, AutosaveCheckpointKind kind) {
  policy->scheduled++;
  policy->pending.id = policy->scheduled;
  policy->pending.kind = kind;
  policy->has_pending = 1;
  policy->idle_polls = 0;
}

bool autosave_policy_extraction_pad(AutosavePolicy *policy, double now_s) {
  if (!policy)
    return false;
  if (policy->has_last_pad &&
      now_s - policy->last_pad_s < kAutosaveCooldownSeconds) {
    policy->cooled++;
    return false;
  }
  policy->has_last_pad = 1;
  policy->last_pad_s = now_s;
  schedule(policy, AutosaveCheckpointKind::ExtractionPad);
  return true;
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
