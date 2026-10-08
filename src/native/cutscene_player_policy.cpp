#include "cutscene_player_policy.h"

namespace x2::native {

constexpr size_t kDefaultStepLimit = 4096u;

int cutscene_player_inherits_context(int sequence_active, int owned_parent,
                                     int owned_event, int owned_payload) {
  return sequence_active && (owned_parent || owned_event || owned_payload);
}

static int operations_valid(const CutscenePlayerOps *ops) {
  return ops && ops->active_sequence && ops->control_state &&
         ops->next_owned_fiber && ops->step_owned_fiber &&
         ops->play_deterministic_conversation;
}

static CutscenePlayerResult fail(CutscenePlayerPolicy *policy,
                                 CutscenePlayerResult result) {
  switch (result) {
  case CutscenePlayerResult::BlockedChoice:
    policy->blocked_choices++;
    break;
  case CutscenePlayerResult::NoProgress:
    policy->no_progress++;
    break;
  case CutscenePlayerResult::Runaway:
    policy->runaways++;
    break;
  case CutscenePlayerResult::Error:
    policy->errors++;
    break;
  default:
    break;
  }
  return result;
}

CutscenePlayerResult cutscene_player_finish(CutscenePlayerPolicy *policy,
                                            const CutscenePlayerOps *ops,
                                            void *context) {
  CutsceneSequence sequence = 0;
  size_t limit, steps;
  int active;

  if (!policy)
    return CutscenePlayerResult::Error;
  if (!operations_valid(ops))
    return fail(policy, CutscenePlayerResult::Error);

  policy->requests++;
  active = ops->active_sequence(context, &sequence);
  if (active < 0)
    return fail(policy, CutscenePlayerResult::Error);
  if (!active)
    return CutscenePlayerResult::Inactive;

  policy->invocations++;
  if (ops->control_state(context, sequence) != CutsceneControlState::Locked)
    return fail(policy, CutscenePlayerResult::Error);

  limit = policy->step_limit ? policy->step_limit : kDefaultStepLimit;
  for (steps = 0; steps < limit; steps++) {
    CutsceneConversation conversation = 0;
    CutsceneControlState controls;
    CutsceneFiberStep step;
    CutsceneFiber fiber = 0;
    int available = ops->next_owned_fiber(context, sequence, &fiber);

    if (available < 0)
      return fail(policy, CutscenePlayerResult::Error);
    if (!available)
      return fail(policy, CutscenePlayerResult::NoProgress);

    step = ops->step_owned_fiber(context, sequence, fiber, &conversation);
    switch (step) {
    case CutsceneFiberStep::Advanced:
    case CutsceneFiberStep::Completed:
      policy->authored_steps++;
      break;
    case CutsceneFiberStep::DeterministicConversation:
      if (!ops->play_deterministic_conversation(context, sequence,
                                                conversation))
        return fail(policy, CutscenePlayerResult::Error);
      policy->conversation_payloads++;
      break;
    case CutsceneFiberStep::Choice:
      return fail(policy, CutscenePlayerResult::BlockedChoice);
    case CutsceneFiberStep::NoProgress:
      return fail(policy, CutscenePlayerResult::NoProgress);
    case CutsceneFiberStep::Error:
    default:
      return fail(policy, CutscenePlayerResult::Error);
    }

    controls = ops->control_state(context, sequence);
    if (controls == CutsceneControlState::Unreadable)
      return fail(policy, CutscenePlayerResult::Error);
    if (controls == CutsceneControlState::Released) {
      policy->completed++;
      return CutscenePlayerResult::Completed;
    }
  }

  return fail(policy, CutscenePlayerResult::Runaway);
}

} // namespace x2::native
