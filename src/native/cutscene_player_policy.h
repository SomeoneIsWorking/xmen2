#ifndef X2_CUTSCENE_PLAYER_POLICY_H
#define X2_CUTSCENE_PLAYER_POLICY_H

#include <stddef.h>
#include <stdint.h>

typedef uintptr_t X2CutsceneSequence;
typedef uintptr_t X2CutsceneFiber;
typedef uintptr_t X2CutsceneConversation;

typedef enum X2CutsceneControlState {
  X2_CUTSCENE_CONTROL_UNREADABLE = -1,
  X2_CUTSCENE_CONTROL_LOCKED = 0,
  X2_CUTSCENE_CONTROL_RELEASED = 1
} X2CutsceneControlState;

typedef enum X2CutsceneFiberStep {
  X2_CUTSCENE_FIBER_ERROR = -1,
  X2_CUTSCENE_FIBER_NO_PROGRESS = 0,
  X2_CUTSCENE_FIBER_ADVANCED,
  X2_CUTSCENE_FIBER_COMPLETED,
  X2_CUTSCENE_FIBER_DETERMINISTIC_CONVERSATION,
  X2_CUTSCENE_FIBER_CHOICE
} X2CutsceneFiberStep;

typedef enum X2CutscenePlayerResult {
  X2_CUTSCENE_PLAYER_COMPLETED = 0,
  X2_CUTSCENE_PLAYER_INACTIVE,
  X2_CUTSCENE_PLAYER_BLOCKED_CHOICE,
  X2_CUTSCENE_PLAYER_NO_PROGRESS,
  X2_CUTSCENE_PLAYER_RUNAWAY,
  X2_CUTSCENE_PLAYER_ERROR
} X2CutscenePlayerResult;

typedef struct X2CutscenePlayerOps {
  /* Return one for the authored sequence currently owning player control,
   * zero when no such sequence exists, and minus one when unreadable. */
  int (*active_sequence)(void *context, X2CutsceneSequence *sequence);
  X2CutsceneControlState (*control_state)(void *context,
                                          X2CutsceneSequence sequence);

  /* Select only a runnable fiber descended from `sequence`. Foreign
   * scheduler contexts are outside this interface and must remain intact. */
  int (*next_owned_fiber)(void *context, X2CutsceneSequence sequence,
                          X2CutsceneFiber *fiber);

  /* Execute one operation through the ported BehavEd player. Authored waits
   * are consumed by that player operation without changing a global clock.
   * A conversation step returns its guest payload without choosing it. */
  X2CutsceneFiberStep (*step_owned_fiber)(void *context,
                                          X2CutsceneSequence sequence,
                                          X2CutsceneFiber fiber,
                                          X2CutsceneConversation *conversation);

  /* Execute only a payload already classified as deterministic by the
   * player. Branching payloads never reach this operation. */
  int (*play_deterministic_conversation)(void *context,
                                         X2CutsceneSequence sequence,
                                         X2CutsceneConversation conversation);
} X2CutscenePlayerOps;

typedef struct X2CutscenePlayerPolicy {
  size_t step_limit;
  unsigned requests;
  unsigned invocations;
  unsigned completed;
  unsigned blocked_choices;
  unsigned no_progress;
  unsigned runaways;
  unsigned errors;
  unsigned long authored_steps;
  unsigned long conversation_payloads;
} X2CutscenePlayerPolicy;

/* A newly allocated BehavEd context inherits only through a causal operation
 * already owned by the sequence. Merely existing during the same control-lock
 * epoch does not adopt unrelated game work. */
int x2_cutscene_player_inherits_context(int sequence_active, int owned_parent,
                                        int owned_event, int owned_payload);

/* Execute one active authored sequence synchronously until its own commands
 * restore player control. This orchestrator has no guest-clock, frame, world,
 * or scheduler-deadline operation: those are not valid ways to finish it. */
X2CutscenePlayerResult x2_cutscene_player_finish(X2CutscenePlayerPolicy *policy,
                                                 const X2CutscenePlayerOps *ops,
                                                 void *context);

#endif
