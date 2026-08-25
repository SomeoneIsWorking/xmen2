#include "conversation_resume_policy.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(expr) check(!!(expr), #expr, __LINE__)

static void check(int passed, const char *expression, int line)
{
    if (passed) return;
    fprintf(stderr, "line %d: CHECK(%s) failed\n", line, expression);
    failures++;
}

static void deterministic_resume_holds_through_locked_gap(void)
{
    X2ConversationResumePolicy policy;
    memset(&policy, 0, sizeof policy);

    x2_conversation_resume_policy_arm(&policy, 20.0);
    x2_conversation_resume_policy_observe(
        &policy, 0, 0, 1, CONVERSATION_SKIP_RESPONSE_WAITING, 21.0);
    CHECK(policy.state == X2_CONVERSATION_RESUME_WAITING);
    x2_conversation_resume_policy_observe(
        &policy, 1, 0, 1, CONVERSATION_SKIP_RESPONSE_DETERMINISTIC, 22.0);
    CHECK(x2_conversation_resume_policy_should_advance(&policy));
    x2_conversation_resume_policy_note_advance(&policy);
    CHECK(policy.advances == 1u);

    /* A hidden gap while the control lock holds is the walk between two
       records of the same authored sequence: the policy holds, however long
       the gap, and advances the next record when it appears. */
    x2_conversation_resume_policy_observe(
        &policy, 0, 0, 1, CONVERSATION_SKIP_RESPONSE_WAITING, 23.0);
    CHECK(policy.state == X2_CONVERSATION_RESUME_SKIPPING);
    x2_conversation_resume_policy_observe(
        &policy, 0, 0, 1, CONVERSATION_SKIP_RESPONSE_WAITING,
        23.0 + CONVERSATION_RESUME_WAIT_SECONDS + 1.0);
    CHECK(policy.state == X2_CONVERSATION_RESUME_SKIPPING);
    x2_conversation_resume_policy_observe(
        &policy, 1, 0, 1, CONVERSATION_SKIP_RESPONSE_DETERMINISTIC,
        23.0 + CONVERSATION_RESUME_WAIT_SECONDS + 2.0);
    CHECK(x2_conversation_resume_policy_should_advance(&policy));

    /* The first UNLOCKED hidden observation is authored cleanup: retire. */
    x2_conversation_resume_policy_observe(
        &policy, 0, 0, 0, CONVERSATION_SKIP_RESPONSE_WAITING, 25.0);
    CHECK(policy.state == X2_CONVERSATION_RESUME_IDLE);
    CHECK(policy.skipped == 1u);
    CHECK(!x2_conversation_resume_policy_should_advance(&policy));
}

static void unowned_gap_and_choice_hand_back(void)
{
    X2ConversationResumePolicy policy;
    memset(&policy, 0, sizeof policy);

    x2_conversation_resume_policy_arm(&policy, 0.0);
    x2_conversation_resume_policy_observe(
        &policy, 1, 0, 0, CONVERSATION_SKIP_RESPONSE_DETERMINISTIC, 1.0);
    x2_conversation_resume_policy_observe(
        &policy, 0, 0, 0, CONVERSATION_SKIP_RESPONSE_WAITING, 2.0);
    CHECK(policy.state == X2_CONVERSATION_RESUME_IDLE);
    CHECK(policy.retired == 1u);

    x2_conversation_resume_policy_arm(&policy, 3.0);
    x2_conversation_resume_policy_observe(
        &policy, 1, 0, 1, CONVERSATION_SKIP_RESPONSE_CHOICE, 4.0);
    CHECK(policy.state == X2_CONVERSATION_RESUME_IDLE);
    CHECK(policy.handed_back == 1u);
}

static void timeout_precedes_later_conversation(void)
{
    X2ConversationResumePolicy policy;
    memset(&policy, 0, sizeof policy);

    /* A locked observation never ages out; an unlocked one past the bound
       does, before any later conversation is classified. */
    x2_conversation_resume_policy_arm(&policy, 5.0);
    x2_conversation_resume_policy_observe(
        &policy, 0, 0, 1, CONVERSATION_SKIP_RESPONSE_UNREADABLE, 15.0);
    CHECK(policy.state != X2_CONVERSATION_RESUME_IDLE);
    x2_conversation_resume_policy_observe(
        &policy, 0, 0, 0, CONVERSATION_SKIP_RESPONSE_UNREADABLE, 15.0);
    CHECK(policy.state == X2_CONVERSATION_RESUME_IDLE);
    CHECK(policy.expired == 1u);

    x2_conversation_resume_policy_arm(&policy, 20.0);
    x2_conversation_resume_policy_observe(
        &policy, 1, 0, 0, CONVERSATION_SKIP_RESPONSE_DETERMINISTIC,
        20.0 + CONVERSATION_RESUME_WAIT_SECONDS + 0.001);
    CHECK(policy.state == X2_CONVERSATION_RESUME_IDLE);
    CHECK(!x2_conversation_resume_policy_should_advance(&policy));
    CHECK(policy.expired == 2u);
}

static void ending_and_manual_override_retire(void)
{
    X2ConversationResumePolicy policy;
    memset(&policy, 0, sizeof policy);

    /* Ending retires once control is back; an ending inside a still-locked
       sequence is the boundary between records and holds. */
    x2_conversation_resume_policy_arm(&policy, 0.0);
    x2_conversation_resume_policy_observe(
        &policy, 0, 1, 1, CONVERSATION_SKIP_RESPONSE_WAITING, 1.0);
    CHECK(policy.state != X2_CONVERSATION_RESUME_IDLE);
    x2_conversation_resume_policy_observe(
        &policy, 0, 1, 0, CONVERSATION_SKIP_RESPONSE_WAITING, 1.5);
    CHECK(policy.state == X2_CONVERSATION_RESUME_IDLE);
    CHECK(policy.retired == 1u);

    x2_conversation_resume_policy_arm(&policy, 2.0);
    x2_conversation_resume_policy_manual_override(&policy);
    CHECK(policy.state == X2_CONVERSATION_RESUME_IDLE);
    CHECK(policy.manual_overrides == 1u);
}

int main(void)
{
    deterministic_resume_holds_through_locked_gap();
    unowned_gap_and_choice_hand_back();
    timeout_precedes_later_conversation();
    ending_and_manual_override_retire();
    if (failures)
        fprintf(stderr, "%d conversation-resume policy check(s) failed\n",
                failures);
    return failures != 0;
}
