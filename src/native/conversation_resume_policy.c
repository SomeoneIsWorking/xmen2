#include "conversation_resume_policy.h"

static void retire(X2ConversationResumePolicy *policy)
{
    policy->state = X2_CONVERSATION_RESUME_IDLE;
    policy->last_deterministic = 0;
}

void x2_conversation_resume_policy_arm(X2ConversationResumePolicy *policy,
                                       double now_s)
{
    policy->state = X2_CONVERSATION_RESUME_WAITING;
    policy->armed_at_s = now_s;
    policy->last_deterministic = 0;
    policy->armed++;
}

void x2_conversation_resume_policy_observe(
    X2ConversationResumePolicy *policy, int visible, int ending,
    int controls_locked, ConversationSkipResponse response, double now_s)
{
    if (policy->state == X2_CONVERSATION_RESUME_IDLE) return;
    policy->observations++;

    /* Expiry precedes classification, but a continuous control lock means
       the restored authored sequence still owns input: its records arrive
       through hidden gaps (the walk between two conversations outlasts any
       fixed bound), so only an unlocked observation can age out. */
    if (x2_conversation_resume_policy_expire(policy, now_s, controls_locked))
        return;

    if (policy->state == X2_CONVERSATION_RESUME_WAITING && !visible &&
        !ending) {
        policy->last_deterministic = 0;
        return;
    }

    if (!visible || ending) {
        policy->last_deterministic = 0;
        if (controls_locked) return;
        policy->retired++;
        if (policy->state == X2_CONVERSATION_RESUME_SKIPPING)
            policy->skipped++;
        retire(policy);
        return;
    }

    if (response == CONVERSATION_SKIP_RESPONSE_CHOICE) {
        policy->handed_back++;
        retire(policy);
        return;
    }
    if (response == CONVERSATION_SKIP_RESPONSE_DETERMINISTIC) {
        policy->state = X2_CONVERSATION_RESUME_SKIPPING;
        policy->last_deterministic = 1;
        return;
    }

    policy->last_deterministic = 0;
}

int x2_conversation_resume_policy_expire(X2ConversationResumePolicy *policy,
                                         double now_s, int controls_locked)
{
    if (policy->state == X2_CONVERSATION_RESUME_IDLE) return 0;
    if (controls_locked) return 0;
    if (now_s - policy->armed_at_s < CONVERSATION_RESUME_WAIT_SECONDS)
        return 0;
    policy->expired++;
    retire(policy);
    return 1;
}

int x2_conversation_resume_policy_should_advance(
    const X2ConversationResumePolicy *policy)
{
    return policy->state == X2_CONVERSATION_RESUME_SKIPPING &&
           policy->last_deterministic;
}

int x2_conversation_resume_policy_active(
    const X2ConversationResumePolicy *policy)
{
    return policy->state != X2_CONVERSATION_RESUME_IDLE;
}

void x2_conversation_resume_policy_note_advance(
    X2ConversationResumePolicy *policy)
{
    if (x2_conversation_resume_policy_should_advance(policy))
        policy->advances++;
}

void x2_conversation_resume_policy_manual_override(
    X2ConversationResumePolicy *policy)
{
    if (!x2_conversation_resume_policy_active(policy)) return;
    policy->manual_overrides++;
    retire(policy);
}
