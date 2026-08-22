#ifndef X2_AUTOSAVE_POLICY_H
#define X2_AUTOSAVE_POLICY_H

#include <stdint.h>

typedef enum {
    X2_AUTOSAVE_CHECKPOINT_NONE = 0,
    X2_AUTOSAVE_CHECKPOINT_MAP_LOAD,
    X2_AUTOSAVE_CHECKPOINT_ZONE_LOAD,
    X2_AUTOSAVE_CHECKPOINT_EXTRACTION
} X2AutosaveCheckpointKind;

typedef struct {
    uint64_t id;
    X2AutosaveCheckpointKind kind;
} X2AutosaveCheckpoint;

typedef enum {
    X2_AUTOSAVE_REQUEST_INVALID = 0,
    X2_AUTOSAVE_REQUEST_QUEUED,
    X2_AUTOSAVE_REQUEST_DUPLICATE,
    X2_AUTOSAVE_REQUEST_STALE
} X2AutosaveRequestResult;

typedef enum {
    X2_AUTOSAVE_POLL_IDLE = 0,
    X2_AUTOSAVE_POLL_DEFERRED,
    X2_AUTOSAVE_POLL_AWAITING_RESULT,
    X2_AUTOSAVE_POLL_FIRE
} X2AutosavePollResult;

typedef enum {
    X2_AUTOSAVE_FINISH_NONE = 0,
    X2_AUTOSAVE_FINISH_SUCCEEDED,
    X2_AUTOSAVE_FINISH_FAILED
} X2AutosaveFinishResult;

typedef struct {
    uint64_t checkpoint_id;
    X2AutosaveFinishResult result;
} X2AutosaveCompletion;

typedef struct {
    uint32_t save_manager_mode;
    int map_nosave;
    int transition_stable;
} X2AutosaveGates;

typedef struct {
    uint64_t latest_requested_id;
    X2AutosaveCheckpoint pending;
    X2AutosaveCheckpoint active;
    X2AutosaveCheckpoint last_finished;
    X2AutosaveFinishResult last_finish_result;
    int has_requested;
    int has_pending;
    int has_active;
    int has_finished;
} X2AutosavePolicy;

void x2_autosave_policy_init(X2AutosavePolicy *policy);

/* IDs are process-monotonic. A newer request replaces an older request that
   has not fired; a request already handed to the save owner is never replaced. */
X2AutosaveRequestResult
x2_autosave_policy_request(X2AutosavePolicy *policy,
                           X2AutosaveCheckpoint checkpoint);

/* FIRE transfers one checkpoint to the save owner exactly once. The owner must
   later call finish with either success or failure before another can fire. */
X2AutosavePollResult
x2_autosave_policy_poll(X2AutosavePolicy *policy, X2AutosaveGates gates,
                        X2AutosaveCheckpoint *checkpoint);

/* Both success and failure settle the active checkpoint. Neither outcome
   requeues it; only a newer checkpoint request can produce another FIRE. */
int x2_autosave_policy_finish(X2AutosavePolicy *policy,
                              X2AutosaveCompletion completion);

#endif /* X2_AUTOSAVE_POLICY_H */
