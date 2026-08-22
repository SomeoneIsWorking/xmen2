#include "conversation_skip_policy.h"

int conversation_skip_policy_is_authored(int camera_owned,
                                         int controls_locked)
{
    return camera_owned || controls_locked;
}

ConversationSkipDecision conversation_skip_policy_update(
    ConversationSkipPolicy *policy, int visible, int authored,
    int continuation_locked, int skip_pressed,
    ConversationSkipResponse response)
{
    if (!visible) {
        if (!continuation_locked) policy->active = 0;
        return CONVERSATION_SKIP_NONE;
    }
    if (skip_pressed) {
        if (authored) {
            if (!policy->active) policy->requests++;
            policy->active = 1;
        } else {
            policy->ignored++;
        }
    }
    if (!policy->active) return CONVERSATION_SKIP_NONE;
    if (!authored || response == CONVERSATION_SKIP_RESPONSE_UNREADABLE) {
        policy->active = 0;
        policy->blocked++;
        return CONVERSATION_SKIP_BLOCKED;
    }
    if (response == CONVERSATION_SKIP_RESPONSE_CHOICE) {
        policy->active = 0;
        policy->blocked++;
        return CONVERSATION_SKIP_BLOCKED;
    }
    if (response == CONVERSATION_SKIP_RESPONSE_DETERMINISTIC) {
        policy->advances++;
        return CONVERSATION_SKIP_ADVANCE;
    }
    return CONVERSATION_SKIP_NONE;
}
