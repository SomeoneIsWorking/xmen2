#include "autosave_policy.h"

/* Dusklight's CC0 src/dusk/autosave.cpp supplied the asynchronous
   request -> write -> completion ownership shape. X-Men's verified checkpoint
   is narrower: successful retail map load, cancelled by main-menu Show. */

void x2_autosave_policy_init(X2AutosavePolicy *policy)
{
    if (policy) *policy = (X2AutosavePolicy){0};
}

void x2_autosave_policy_map_return(X2AutosavePolicy *policy, int succeeded)
{
    if (!policy) return;
    policy->map_returns++;
    if (!succeeded) return;
    policy->successful_map_returns++;
    policy->pending.id = policy->successful_map_returns;
    policy->pending.kind = X2_AUTOSAVE_CHECKPOINT_MAP_LOAD;
    policy->has_pending = 1;
    policy->idle_polls = 0;
    policy->scheduled++;
}

void x2_autosave_policy_menu_show(X2AutosavePolicy *policy)
{
    if (!policy || !policy->has_pending) return;
    policy->has_pending = 0;
    policy->idle_polls = 0;
    policy->cancelled_menu++;
}

X2AutosavePollResult
x2_autosave_policy_poll(X2AutosavePolicy *policy, uint32_t manager_mode,
                        X2AutosaveCheckpoint *checkpoint)
{
    if (!policy) return X2_AUTOSAVE_POLL_IDLE;
    if (policy->has_active) return X2_AUTOSAVE_POLL_AWAITING_RESULT;
    if (!policy->has_pending) return X2_AUTOSAVE_POLL_IDLE;
    if (manager_mode != 0u) {
        policy->idle_polls = 0;
        policy->deferred_polls++;
        return X2_AUTOSAVE_POLL_DEFERRED;
    }
    policy->idle_polls++;
    if (policy->idle_polls < X2_AUTOSAVE_IDLE_POLLS) {
        policy->deferred_polls++;
        return X2_AUTOSAVE_POLL_DEFERRED;
    }
    policy->active = policy->pending;
    policy->has_active = 1;
    policy->has_pending = 0;
    policy->attempts++;
    if (checkpoint) *checkpoint = policy->active;
    return X2_AUTOSAVE_POLL_FIRE;
}

int x2_autosave_policy_finish(X2AutosavePolicy *policy,
                              uint64_t checkpoint_id, int succeeded)
{
    if (!policy || !policy->has_active
        || policy->active.id != checkpoint_id)
        return 0;
    policy->has_active = 0;
    if (succeeded) policy->successes++;
    else policy->failures++;
    return 1;
}
