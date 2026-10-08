#pragma once

#include <cstddef>
#include <cstdint>

namespace x2::native {

using CutsceneSequence = uintptr_t;
using CutsceneFiber = uintptr_t;
using CutsceneConversation = uintptr_t;

enum class CutsceneControlState { Unreadable = -1, Locked = 0, Released = 1 };

enum class CutsceneFiberStep {
  Error = -1,
  NoProgress = 0,
  Advanced,
  Completed,
  DeterministicConversation,
  Choice
};

enum class CutscenePlayerResult {
  Completed = 0,
  Inactive,
  BlockedChoice,
  NoProgress,
  Runaway,
  Error
};

struct CutscenePlayerOps {
  /* Return one for the authored sequence currently owning player control,
   * zero when no such sequence exists, and minus one when unreadable. */
  int (*active_sequence)(void *context, CutsceneSequence *sequence);
  CutsceneControlState (*control_state)(void *context,
                                        CutsceneSequence sequence);

  /* Select only a runnable fiber descended from `sequence`. Foreign
   * scheduler contexts are outside this interface and must remain intact. */
  int (*next_owned_fiber)(void *context, CutsceneSequence sequence,
                          CutsceneFiber *fiber);

  /* Execute one operation through the ported BehavEd player. Authored waits
   * are consumed by that player operation without changing a global clock.
   * A conversation step returns its guest payload without choosing it. */
  CutsceneFiberStep (*step_owned_fiber)(void *context,
                                        CutsceneSequence sequence,
                                        CutsceneFiber fiber,
                                        CutsceneConversation *conversation);

  /* Execute only a payload already classified as deterministic by the
   * player. Branching payloads never reach this operation. */
  int (*play_deterministic_conversation)(void *context,
                                         CutsceneSequence sequence,
                                         CutsceneConversation conversation);
};

struct CutscenePlayerPolicy {
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
};

/* A newly allocated BehavEd context inherits only through a causal operation
 * already owned by the sequence. Merely existing during the same control-lock
 * epoch does not adopt unrelated game work. */
int cutscene_player_inherits_context(int sequence_active, int owned_parent,
                                     int owned_event, int owned_payload);

/* Execute one active authored sequence synchronously until its own commands
 * restore player control. This orchestrator has no guest-clock, frame, world,
 * or scheduler-deadline operation: those are not valid ways to finish it. */
CutscenePlayerResult cutscene_player_finish(CutscenePlayerPolicy *policy,
                                            const CutscenePlayerOps *ops,
                                            void *context);

} // namespace x2::native
