#include "cutscene_player_policy.h"

#define X2_CUTSCENE_DEFAULT_STEP_LIMIT 4096u

int x2_cutscene_player_inherits_context(int sequence_active, int owned_parent,
                                        int owned_event, int owned_payload) {
  return sequence_active && (owned_parent || owned_event || owned_payload);
}

static int operations_valid(const X2CutscenePlayerOps *ops) {
  return ops && ops->active_sequence && ops->control_state &&
         ops->next_owned_fiber && ops->step_owned_fiber &&
         ops->play_deterministic_conversation;
}

static X2CutscenePlayerResult fail(X2CutscenePlayerPolicy *policy,
                                   X2CutscenePlayerResult result) {
  switch (result) {
  case X2_CUTSCENE_PLAYER_BLOCKED_CHOICE:
    policy->blocked_choices++;
    break;
  case X2_CUTSCENE_PLAYER_NO_PROGRESS:
    policy->no_progress++;
    break;
  case X2_CUTSCENE_PLAYER_RUNAWAY:
    policy->runaways++;
    break;
  case X2_CUTSCENE_PLAYER_ERROR:
    policy->errors++;
    break;
  default:
    break;
  }
  return result;
}

X2CutscenePlayerResult x2_cutscene_player_finish(X2CutscenePlayerPolicy *policy,
                                                 const X2CutscenePlayerOps *ops,
                                                 void *context) {
  X2CutsceneSequence sequence = 0;
  size_t limit, steps;
  int active;

  if (!policy)
    return X2_CUTSCENE_PLAYER_ERROR;
  if (!operations_valid(ops))
    return fail(policy, X2_CUTSCENE_PLAYER_ERROR);

  policy->requests++;
  active = ops->active_sequence(context, &sequence);
  if (active < 0)
    return fail(policy, X2_CUTSCENE_PLAYER_ERROR);
  if (!active)
    return X2_CUTSCENE_PLAYER_INACTIVE;

  policy->invocations++;
  if (ops->control_state(context, sequence) != X2_CUTSCENE_CONTROL_LOCKED)
    return fail(policy, X2_CUTSCENE_PLAYER_ERROR);

  limit =
      policy->step_limit ? policy->step_limit : X2_CUTSCENE_DEFAULT_STEP_LIMIT;
  for (steps = 0; steps < limit; steps++) {
    X2CutsceneConversation conversation = 0;
    X2CutsceneControlState controls;
    X2CutsceneFiberStep step;
    X2CutsceneFiber fiber = 0;
    int available = ops->next_owned_fiber(context, sequence, &fiber);

    if (available < 0)
      return fail(policy, X2_CUTSCENE_PLAYER_ERROR);
    if (!available)
      return fail(policy, X2_CUTSCENE_PLAYER_NO_PROGRESS);

    step = ops->step_owned_fiber(context, sequence, fiber, &conversation);
    switch (step) {
    case X2_CUTSCENE_FIBER_ADVANCED:
    case X2_CUTSCENE_FIBER_COMPLETED:
      policy->authored_steps++;
      break;
    case X2_CUTSCENE_FIBER_DETERMINISTIC_CONVERSATION:
      if (!ops->play_deterministic_conversation(context, sequence,
                                                conversation))
        return fail(policy, X2_CUTSCENE_PLAYER_ERROR);
      policy->conversation_payloads++;
      break;
    case X2_CUTSCENE_FIBER_CHOICE:
      return fail(policy, X2_CUTSCENE_PLAYER_BLOCKED_CHOICE);
    case X2_CUTSCENE_FIBER_NO_PROGRESS:
      return fail(policy, X2_CUTSCENE_PLAYER_NO_PROGRESS);
    case X2_CUTSCENE_FIBER_ERROR:
    default:
      return fail(policy, X2_CUTSCENE_PLAYER_ERROR);
    }

    controls = ops->control_state(context, sequence);
    if (controls == X2_CUTSCENE_CONTROL_UNREADABLE)
      return fail(policy, X2_CUTSCENE_PLAYER_ERROR);
    if (controls == X2_CUTSCENE_CONTROL_RELEASED) {
      policy->completed++;
      return X2_CUTSCENE_PLAYER_COMPLETED;
    }
  }

  return fail(policy, X2_CUTSCENE_PLAYER_RUNAWAY);
}
