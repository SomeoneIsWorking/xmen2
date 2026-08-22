#include "autosave_policy.h"

#include <assert.h>
#include <stdio.h>

static int checks;

#define CHECK(c) do { assert(c); checks++; } while (0)

static X2AutosaveGates ready(void)
{
    X2AutosaveGates gates = {0};

    gates.transition_stable = 1;
    return gates;
}

static X2AutosaveCheckpoint fire(X2AutosavePolicy *policy,
                                 X2AutosaveGates gates)
{
    X2AutosaveCheckpoint checkpoint = {0};

    CHECK(x2_autosave_policy_poll(policy, gates, &checkpoint)
          == X2_AUTOSAVE_POLL_FIRE);
    return checkpoint;
}

static X2AutosaveRequestResult request(X2AutosavePolicy *policy, uint64_t id,
                                       X2AutosaveCheckpointKind kind)
{
    X2AutosaveCheckpoint checkpoint = {id, kind};

    return x2_autosave_policy_request(policy, checkpoint);
}

static int finish(X2AutosavePolicy *policy, uint64_t id,
                  X2AutosaveFinishResult result)
{
    X2AutosaveCompletion completion = {id, result};

    return x2_autosave_policy_finish(policy, completion);
}

static void test_deferred_then_fired_once(void)
{
    X2AutosavePolicy policy;
    X2AutosaveCheckpoint checkpoint;
    X2AutosaveGates gates = ready();

    x2_autosave_policy_init(&policy);
    CHECK(x2_autosave_policy_poll(&policy, gates, NULL)
          == X2_AUTOSAVE_POLL_IDLE);
    CHECK(request(&policy, 10, X2_AUTOSAVE_CHECKPOINT_EXTRACTION)
          == X2_AUTOSAVE_REQUEST_QUEUED);

    gates.save_manager_mode = 4;
    CHECK(x2_autosave_policy_poll(&policy, gates, NULL)
          == X2_AUTOSAVE_POLL_DEFERRED);
    gates.save_manager_mode = 0;
    gates.map_nosave = 1;
    CHECK(x2_autosave_policy_poll(&policy, gates, NULL)
          == X2_AUTOSAVE_POLL_DEFERRED);
    gates.map_nosave = 0;
    gates.transition_stable = 0;
    CHECK(x2_autosave_policy_poll(&policy, gates, NULL)
          == X2_AUTOSAVE_POLL_DEFERRED);

    gates.transition_stable = 1;
    checkpoint = fire(&policy, gates);
    CHECK(checkpoint.id == 10);
    CHECK(checkpoint.kind == X2_AUTOSAVE_CHECKPOINT_EXTRACTION);
    CHECK(x2_autosave_policy_poll(&policy, gates, NULL)
          == X2_AUTOSAVE_POLL_AWAITING_RESULT);
}

static void test_failure_does_not_retry(void)
{
    X2AutosavePolicy policy;
    X2AutosaveCheckpoint checkpoint;
    X2AutosaveGates gates = ready();

    x2_autosave_policy_init(&policy);
    CHECK(request(&policy, 20, X2_AUTOSAVE_CHECKPOINT_MAP_LOAD)
          == X2_AUTOSAVE_REQUEST_QUEUED);
    checkpoint = fire(&policy, gates);

    CHECK(!finish(&policy, checkpoint.id + 1, X2_AUTOSAVE_FINISH_FAILED));
    CHECK(x2_autosave_policy_poll(&policy, gates, NULL)
          == X2_AUTOSAVE_POLL_AWAITING_RESULT);
    CHECK(finish(&policy, checkpoint.id, X2_AUTOSAVE_FINISH_FAILED));
    CHECK(policy.has_finished);
    CHECK(policy.last_finished.id == 20);
    CHECK(policy.last_finish_result == X2_AUTOSAVE_FINISH_FAILED);
    CHECK(x2_autosave_policy_poll(&policy, gates, NULL)
          == X2_AUTOSAVE_POLL_IDLE);
    CHECK(request(&policy, 20, X2_AUTOSAVE_CHECKPOINT_MAP_LOAD)
          == X2_AUTOSAVE_REQUEST_DUPLICATE);
    CHECK(x2_autosave_policy_poll(&policy, gates, NULL)
          == X2_AUTOSAVE_POLL_IDLE);

    CHECK(request(&policy, 21, X2_AUTOSAVE_CHECKPOINT_ZONE_LOAD)
          == X2_AUTOSAVE_REQUEST_QUEUED);
    checkpoint = fire(&policy, gates);
    CHECK(checkpoint.id == 21);
    CHECK(finish(&policy, checkpoint.id, X2_AUTOSAVE_FINISH_SUCCEEDED));
}

static void test_newest_pending_checkpoint_wins(void)
{
    X2AutosavePolicy policy;
    X2AutosaveCheckpoint checkpoint;
    X2AutosaveGates gates = ready();

    x2_autosave_policy_init(&policy);
    gates.save_manager_mode = 3;
    CHECK(request(&policy, 30, X2_AUTOSAVE_CHECKPOINT_MAP_LOAD)
          == X2_AUTOSAVE_REQUEST_QUEUED);
    CHECK(request(&policy, 31, X2_AUTOSAVE_CHECKPOINT_EXTRACTION)
          == X2_AUTOSAVE_REQUEST_QUEUED);
    CHECK(request(&policy, 31, X2_AUTOSAVE_CHECKPOINT_ZONE_LOAD)
          == X2_AUTOSAVE_REQUEST_DUPLICATE);
    CHECK(request(&policy, 29, X2_AUTOSAVE_CHECKPOINT_ZONE_LOAD)
          == X2_AUTOSAVE_REQUEST_STALE);
    CHECK(x2_autosave_policy_poll(&policy, gates, NULL)
          == X2_AUTOSAVE_POLL_DEFERRED);

    gates.save_manager_mode = 0;
    checkpoint = fire(&policy, gates);
    CHECK(checkpoint.id == 31);
    CHECK(checkpoint.kind == X2_AUTOSAVE_CHECKPOINT_EXTRACTION);
    CHECK(finish(&policy, checkpoint.id, X2_AUTOSAVE_FINISH_SUCCEEDED));
}

static void test_new_checkpoint_waits_for_active_result(void)
{
    X2AutosavePolicy policy;
    X2AutosaveCheckpoint checkpoint;
    X2AutosaveGates gates = ready();

    x2_autosave_policy_init(&policy);
    CHECK(request(&policy, 40, X2_AUTOSAVE_CHECKPOINT_MAP_LOAD)
          == X2_AUTOSAVE_REQUEST_QUEUED);
    checkpoint = fire(&policy, gates);
    CHECK(checkpoint.id == 40);
    CHECK(request(&policy, 41, X2_AUTOSAVE_CHECKPOINT_ZONE_LOAD)
          == X2_AUTOSAVE_REQUEST_QUEUED);
    CHECK(x2_autosave_policy_poll(&policy, gates, NULL)
          == X2_AUTOSAVE_POLL_AWAITING_RESULT);
    CHECK(finish(&policy, 40, X2_AUTOSAVE_FINISH_SUCCEEDED));

    checkpoint = fire(&policy, gates);
    CHECK(checkpoint.id == 41);
    CHECK(checkpoint.kind == X2_AUTOSAVE_CHECKPOINT_ZONE_LOAD);
}

static void test_invalid_inputs(void)
{
    X2AutosavePolicy policy;
    X2AutosaveGates gates = ready();

    x2_autosave_policy_init(&policy);
    CHECK(request(&policy, 1, X2_AUTOSAVE_CHECKPOINT_NONE)
          == X2_AUTOSAVE_REQUEST_INVALID);
    CHECK(request(NULL, 1, X2_AUTOSAVE_CHECKPOINT_MAP_LOAD)
          == X2_AUTOSAVE_REQUEST_INVALID);
    CHECK(x2_autosave_policy_poll(NULL, gates, NULL)
          == X2_AUTOSAVE_POLL_IDLE);
    CHECK(!finish(&policy, 1, X2_AUTOSAVE_FINISH_SUCCEEDED));
}

int main(void)
{
    test_deferred_then_fired_once();
    test_failure_does_not_retry();
    test_newest_pending_checkpoint_wins();
    test_new_checkpoint_waits_for_active_result();
    test_invalid_inputs();
    printf("test_autosave_policy: %d checks passed\n", checks);
    return 0;
}
