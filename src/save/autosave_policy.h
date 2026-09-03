#ifndef X2_AUTOSAVE_POLICY_H
#define X2_AUTOSAVE_POLICY_H

#include <stdint.h>

#define X2_AUTOSAVE_IDLE_POLLS 64u

typedef enum {
  X2_AUTOSAVE_CHECKPOINT_NONE = 0,
  X2_AUTOSAVE_CHECKPOINT_MAP_LOAD
} X2AutosaveCheckpointKind;

typedef struct {
  uint64_t id;
  X2AutosaveCheckpointKind kind;
} X2AutosaveCheckpoint;

typedef enum {
  X2_AUTOSAVE_POLL_IDLE = 0,
  X2_AUTOSAVE_POLL_DEFERRED,
  X2_AUTOSAVE_POLL_AWAITING_RESULT,
  X2_AUTOSAVE_POLL_FIRE
} X2AutosavePollResult;

typedef struct {
  uint64_t map_returns;
  uint64_t successful_map_returns;
  uint64_t scheduled;
  uint64_t cancelled_menu;
  uint64_t deferred_polls;
  uint64_t attempts;
  uint64_t successes;
  uint64_t failures;
  X2AutosaveCheckpoint pending;
  X2AutosaveCheckpoint active;
  unsigned idle_polls;
  int has_pending;
  int has_active;
} X2AutosavePolicy;

void x2_autosave_policy_init(X2AutosavePolicy *policy);
void x2_autosave_policy_map_return(X2AutosavePolicy *policy, int succeeded);
void x2_autosave_policy_menu_show(X2AutosavePolicy *policy);
X2AutosavePollResult x2_autosave_policy_poll(X2AutosavePolicy *policy,
                                             uint32_t manager_mode,
                                             X2AutosaveCheckpoint *checkpoint);
int x2_autosave_policy_finish(X2AutosavePolicy *policy, uint64_t checkpoint_id,
                              int succeeded);

#endif
