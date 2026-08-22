#include "autosave_policy.h"

/* Dusklight's CC0 src/dusk/autosave.cpp was consulted for ownership and the
   asynchronous request -> write -> completion shape. Its TP-specific player,
   stage and memory-card rules do not transfer; this policy instead exposes the
   X-Men retail manager/map/transition gates that the runtime owner must prove. */

void x2_autosave_policy_init(X2AutosavePolicy *policy)
{
    if (policy)
        *policy = (X2AutosavePolicy){0};
}

X2AutosaveRequestResult
x2_autosave_policy_request(X2AutosavePolicy *policy,
                           X2AutosaveCheckpoint checkpoint)
{
    if (!policy || checkpoint.kind == X2_AUTOSAVE_CHECKPOINT_NONE)
        return X2_AUTOSAVE_REQUEST_INVALID;

    if (policy->has_requested) {
        if (checkpoint.id == policy->latest_requested_id)
            return X2_AUTOSAVE_REQUEST_DUPLICATE;
        if (checkpoint.id < policy->latest_requested_id)
            return X2_AUTOSAVE_REQUEST_STALE;
    }

    policy->latest_requested_id = checkpoint.id;
    policy->has_requested = 1;
    policy->pending = checkpoint;
    policy->has_pending = 1;
    return X2_AUTOSAVE_REQUEST_QUEUED;
}

X2AutosavePollResult
x2_autosave_policy_poll(X2AutosavePolicy *policy, X2AutosaveGates gates,
                        X2AutosaveCheckpoint *checkpoint)
{
    if (!policy)
        return X2_AUTOSAVE_POLL_IDLE;
    if (policy->has_active)
        return X2_AUTOSAVE_POLL_AWAITING_RESULT;
    if (!policy->has_pending)
        return X2_AUTOSAVE_POLL_IDLE;
    if (gates.save_manager_mode != 0 || gates.map_nosave
        || !gates.transition_stable)
        return X2_AUTOSAVE_POLL_DEFERRED;

    policy->active = policy->pending;
    policy->has_active = 1;
    policy->has_pending = 0;
    if (checkpoint)
        *checkpoint = policy->active;
    return X2_AUTOSAVE_POLL_FIRE;
}

int x2_autosave_policy_finish(X2AutosavePolicy *policy,
                              X2AutosaveCompletion completion)
{
    if (!policy || !policy->has_active
        || policy->active.id != completion.checkpoint_id)
        return 0;
    if (completion.result != X2_AUTOSAVE_FINISH_SUCCEEDED
        && completion.result != X2_AUTOSAVE_FINISH_FAILED)
        return 0;

    policy->last_finished = policy->active;
    policy->last_finish_result = completion.result;
    policy->has_finished = 1;
    policy->has_active = 0;
    return 1;
}
