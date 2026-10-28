#include "autosave_policy.h"

#include <assert.h>
#include <stdio.h>

static int checks;
#define CHECK(c)                                                               \
  do {                                                                         \
    assert(c);                                                                 \
    checks++;                                                                  \
  } while (0)

static x2::save::AutosaveCheckpoint
poll_until_fire(x2::save::AutosavePolicy *policy) {
  x2::save::AutosaveCheckpoint checkpoint = {0};
  unsigned poll;
  for (poll = 1u; poll < x2::save::kAutosaveIdlePolls; poll++)
    CHECK(x2::save::autosave_policy_poll(policy, 0u, 1, &checkpoint) ==
          x2::save::AutosavePollResult::Deferred);
  CHECK(x2::save::autosave_policy_poll(policy, 0u, 1, &checkpoint) ==
        x2::save::AutosavePollResult::Fire);
  return checkpoint;
}

static void test_pad_checkpoint_and_menu_cancellation(void) {
  x2::save::AutosavePolicy policy;

  x2::save::autosave_policy_init(&policy);
  x2::save::autosave_policy_extraction_pad(&policy, 5.0);
  CHECK(policy.has_pending &&
        policy.pending.kind == x2::save::AutosaveCheckpointKind::ExtractionPad);
  x2::save::autosave_policy_menu_show(&policy);
  CHECK(!policy.has_pending && policy.cancelled_menu == 1u);
  CHECK(x2::save::autosave_policy_poll(&policy, 0u, 1, NULL) ==
        x2::save::AutosavePollResult::Idle);
}

/* Stepping off and back on within the cooldown takes no second save. */
static void test_pad_cooldown(void) {
  x2::save::AutosavePolicy policy;

  x2::save::autosave_policy_init(&policy);
  CHECK(x2::save::autosave_policy_extraction_pad(&policy, 100.0));
  CHECK(!x2::save::autosave_policy_extraction_pad(&policy, 103.0));
  CHECK(!x2::save::autosave_policy_extraction_pad(&policy, 109.9));
  CHECK(policy.scheduled == 1u && policy.cooled == 2u);
  CHECK(x2::save::autosave_policy_extraction_pad(&policy, 110.0));
  CHECK(policy.scheduled == 2u && policy.pending.id == 2u);
}

static void test_64_consecutive_idle_polls(void) {
  x2::save::AutosavePolicy policy;
  x2::save::AutosaveCheckpoint checkpoint;
  unsigned poll;

  x2::save::autosave_policy_init(&policy);
  x2::save::autosave_policy_extraction_pad(&policy, 0.0);
  for (poll = 0; poll < 32u; poll++)
    CHECK(x2::save::autosave_policy_poll(&policy, 0u, 1, NULL) ==
          x2::save::AutosavePollResult::Deferred);
  CHECK(x2::save::autosave_policy_poll(&policy, 3u, 1, NULL) ==
        x2::save::AutosavePollResult::Deferred);
  CHECK(policy.idle_polls == 0u);
  checkpoint = poll_until_fire(&policy);
  CHECK(checkpoint.id == 1u);
  CHECK(policy.attempts == 1u && policy.deferred_polls == 96u);
  CHECK(x2::save::autosave_policy_poll(&policy, 0u, 1, NULL) ==
        x2::save::AutosavePollResult::AwaitingResult);
  CHECK(!x2::save::autosave_policy_finish(&policy, checkpoint.id + 1u, 1));
  CHECK(x2::save::autosave_policy_finish(&policy, checkpoint.id, 1));
  CHECK(policy.successes == 1u && policy.failures == 0u);
}

/* Dead Zone's opening script parks the party out of sight under a control
   lock; the checkpoint must not fire until the player controls a character
   again, however long the manager has been idle. */
static void test_waits_for_player_control(void) {
  x2::save::AutosavePolicy policy;
  x2::save::AutosaveCheckpoint checkpoint;
  unsigned poll;

  x2::save::autosave_policy_init(&policy);
  x2::save::autosave_policy_extraction_pad(&policy, 0.0);
  for (poll = 0; poll < 4u * x2::save::kAutosaveIdlePolls; poll++)
    CHECK(x2::save::autosave_policy_poll(&policy, 0u, 0, NULL) ==
          x2::save::AutosavePollResult::Deferred);
  CHECK(policy.idle_polls == 0u && policy.attempts == 0u);
  CHECK(policy.control_deferred_polls == 4u * x2::save::kAutosaveIdlePolls);
  for (poll = 0; poll < x2::save::kAutosaveIdlePolls - 1u; poll++)
    CHECK(x2::save::autosave_policy_poll(&policy, 0u, 1, NULL) ==
          x2::save::AutosavePollResult::Deferred);
  /* A lock one poll short of the checkpoint starts the count again. */
  CHECK(x2::save::autosave_policy_poll(&policy, 0u, 0, NULL) ==
        x2::save::AutosavePollResult::Deferred);
  CHECK(policy.idle_polls == 0u);
  checkpoint = poll_until_fire(&policy);
  CHECK(checkpoint.id == 1u && policy.attempts == 1u);
}

static void test_failure_no_retry_and_newer_pad_replaces_pending(void) {
  x2::save::AutosavePolicy policy;
  x2::save::AutosaveCheckpoint checkpoint;

  x2::save::autosave_policy_init(&policy);
  x2::save::autosave_policy_extraction_pad(&policy, 0.0);
  x2::save::autosave_policy_extraction_pad(&policy, 20.0);
  CHECK(policy.scheduled == 2u && policy.pending.id == 2u);
  checkpoint = poll_until_fire(&policy);
  CHECK(checkpoint.id == 2u);
  CHECK(x2::save::autosave_policy_finish(&policy, checkpoint.id, 0));
  CHECK(policy.failures == 1u && !policy.has_pending);
  CHECK(x2::save::autosave_policy_poll(&policy, 0u, 1, NULL) ==
        x2::save::AutosavePollResult::Idle);
  x2::save::autosave_policy_extraction_pad(&policy, 40.0);
  checkpoint = poll_until_fire(&policy);
  CHECK(checkpoint.id == 3u);
}

static void test_invalid_inputs(void) {
  x2::save::autosave_policy_extraction_pad(NULL, 0.0);
  x2::save::autosave_policy_menu_show(NULL);
  CHECK(x2::save::autosave_policy_poll(NULL, 0u, 1, NULL) ==
        x2::save::AutosavePollResult::Idle);
  CHECK(!x2::save::autosave_policy_finish(NULL, 1u, 1));
}

int main(void) {
  test_pad_checkpoint_and_menu_cancellation();
  test_pad_cooldown();
  test_64_consecutive_idle_polls();
  test_waits_for_player_control();
  test_failure_no_retry_and_newer_pad_replaces_pending();
  test_invalid_inputs();
  printf("autosave_policy: %d checks passed\n", checks);
  return 0;
}
