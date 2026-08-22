#include "conversation_skip_policy.h"

#include <assert.h>
#include <stdio.h>

static int checks;
#define CHECK(c) do { assert(c); checks++; } while (0)

int main(void)
{
    ConversationSkipPolicy policy = {0};
    int ordinary = conversation_skip_policy_is_authored(0, 0);

    CHECK(!ordinary);
    CHECK(conversation_skip_policy_update(
              &policy, 1, ordinary, 0, 1,
              CONVERSATION_SKIP_RESPONSE_DETERMINISTIC) ==
          CONVERSATION_SKIP_NONE);
    CHECK(policy.ignored == 1u && !policy.active);
    CHECK(conversation_skip_policy_is_authored(1, 0));
    CHECK(conversation_skip_policy_is_authored(0, 1));

    CHECK(conversation_skip_policy_update(
              &policy, 1, 1, 1, 1, CONVERSATION_SKIP_RESPONSE_WAITING) ==
          CONVERSATION_SKIP_NONE);
    CHECK(policy.active && policy.requests == 1u);
    CHECK(conversation_skip_policy_update(
              &policy, 1, 1, 1, 0, CONVERSATION_SKIP_RESPONSE_DETERMINISTIC) ==
          CONVERSATION_SKIP_ADVANCE);
    CHECK(policy.active && policy.advances == 1u);
    CHECK(conversation_skip_policy_update(
              &policy, 1, 1, 1, 0, CONVERSATION_SKIP_RESPONSE_DETERMINISTIC) ==
          CONVERSATION_SKIP_ADVANCE);
    CHECK(policy.advances == 2u);

    CHECK(conversation_skip_policy_update(
              &policy, 1, 1, 1, 0, CONVERSATION_SKIP_RESPONSE_CHOICE) ==
          CONVERSATION_SKIP_BLOCKED);
    CHECK(!policy.active && policy.blocked == 1u);

    CHECK(conversation_skip_policy_update(
              &policy, 1, 1, 1, 1, CONVERSATION_SKIP_RESPONSE_DETERMINISTIC) ==
          CONVERSATION_SKIP_ADVANCE);
    CHECK(policy.requests == 2u);
    CHECK(conversation_skip_policy_update(
              &policy, 0, 0, 1, 0, CONVERSATION_SKIP_RESPONSE_WAITING) ==
          CONVERSATION_SKIP_NONE);
    CHECK(policy.active); /* lockControls keeps one authored sequence armed */
    CHECK(conversation_skip_policy_update(
              &policy, 0, 0, 0, 0, CONVERSATION_SKIP_RESPONSE_WAITING) ==
          CONVERSATION_SKIP_NONE);
    CHECK(!policy.active);

    CHECK(conversation_skip_policy_update(
              &policy, 1, 1, 1, 1, CONVERSATION_SKIP_RESPONSE_UNREADABLE) ==
          CONVERSATION_SKIP_BLOCKED);
    CHECK(!policy.active && policy.blocked == 2u);

    printf("test_conversation_skip_policy: %d checks passed\n", checks);
    return 0;
}
