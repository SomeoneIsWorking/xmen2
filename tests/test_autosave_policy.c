#include "autosave_policy.h"

#include <assert.h>
#include <stdio.h>

static int checks;
#define CHECK(c)                                                               \
  do {                                                                         \
    assert(c);                                                                 \
    checks++;                                                                  \
  } while (0)

static X2AutosaveCheckpoint poll_until_fire(X2AutosavePolicy *policy) {
  X2AutosaveCheckpoint checkpoint = {0};
  unsigned poll;
  for (poll = 1u; poll < X2_AUTOSAVE_IDLE_POLLS; poll++)
    CHECK(x2_autosave_policy_poll(policy, 0u, &checkpoint) ==
          X2_AUTOSAVE_POLL_DEFERRED);
  CHECK(x2_autosave_policy_poll(policy, 0u, &checkpoint) ==
        X2_AUTOSAVE_POLL_FIRE);
  return checkpoint;
}

static void test_map_checkpoint_and_menu_cancellation(void) {
  X2AutosavePolicy policy;

  x2_autosave_policy_init(&policy);
  x2_autosave_policy_map_return(&policy, 0);
  CHECK(policy.map_returns == 1u && !policy.has_pending);
  x2_autosave_policy_map_return(&policy, 1);
  CHECK(policy.map_returns == 2u && policy.successful_map_returns == 1u);
  CHECK(policy.has_pending &&
        policy.pending.kind == X2_AUTOSAVE_CHECKPOINT_MAP_LOAD);
  x2_autosave_policy_menu_show(&policy);
  CHECK(!policy.has_pending && policy.cancelled_menu == 1u);
  CHECK(x2_autosave_policy_poll(&policy, 0u, NULL) == X2_AUTOSAVE_POLL_IDLE);
}

static void test_64_consecutive_idle_polls(void) {
  X2AutosavePolicy policy;
  X2AutosaveCheckpoint checkpoint;
  unsigned poll;

  x2_autosave_policy_init(&policy);
  x2_autosave_policy_map_return(&policy, 1);
  for (poll = 0; poll < 32u; poll++)
    CHECK(x2_autosave_policy_poll(&policy, 0u, NULL) ==
          X2_AUTOSAVE_POLL_DEFERRED);
  CHECK(x2_autosave_policy_poll(&policy, 3u, NULL) ==
        X2_AUTOSAVE_POLL_DEFERRED);
  CHECK(policy.idle_polls == 0u);
  checkpoint = poll_until_fire(&policy);
  CHECK(checkpoint.id == 1u);
  CHECK(policy.attempts == 1u && policy.deferred_polls == 96u);
  CHECK(x2_autosave_policy_poll(&policy, 0u, NULL) ==
        X2_AUTOSAVE_POLL_AWAITING_RESULT);
  CHECK(!x2_autosave_policy_finish(&policy, checkpoint.id + 1u, 1));
  CHECK(x2_autosave_policy_finish(&policy, checkpoint.id, 1));
  CHECK(policy.successes == 1u && policy.failures == 0u);
}

static void test_failure_no_retry_and_newer_map_replaces_pending(void) {
  X2AutosavePolicy policy;
  X2AutosaveCheckpoint checkpoint;

  x2_autosave_policy_init(&policy);
  x2_autosave_policy_map_return(&policy, 1);
  x2_autosave_policy_map_return(&policy, 1);
  CHECK(policy.scheduled == 2u && policy.pending.id == 2u);
  checkpoint = poll_until_fire(&policy);
  CHECK(checkpoint.id == 2u);
  CHECK(x2_autosave_policy_finish(&policy, checkpoint.id, 0));
  CHECK(policy.failures == 1u && !policy.has_pending);
  CHECK(x2_autosave_policy_poll(&policy, 0u, NULL) == X2_AUTOSAVE_POLL_IDLE);
  x2_autosave_policy_map_return(&policy, 1);
  checkpoint = poll_until_fire(&policy);
  CHECK(checkpoint.id == 3u);
}

static void test_invalid_inputs(void) {
  x2_autosave_policy_map_return(NULL, 1);
  x2_autosave_policy_menu_show(NULL);
  CHECK(x2_autosave_policy_poll(NULL, 0u, NULL) == X2_AUTOSAVE_POLL_IDLE);
  CHECK(!x2_autosave_policy_finish(NULL, 1u, 1));
}

int main(void) {
  test_map_checkpoint_and_menu_cancellation();
  test_64_consecutive_idle_polls();
  test_failure_no_retry_and_newer_map_replaces_pending();
  test_invalid_inputs();
  printf("autosave_policy: %d checks passed\n", checks);
  return 0;
}
