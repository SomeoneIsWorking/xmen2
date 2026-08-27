#include "cutscene_player_policy.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define ARRAY_COUNT(a) (sizeof(a) / sizeof((a)[0]))

enum {
    ROOT_FIBER,
    SPAWN_FIBER,
    WALK_FIBER,
    CLEANUP_FIBER,
    FOREIGN_FIBER,
    FIBER_COUNT
};

typedef enum TestCommand {
    COMMAND_CAMERA_OWNED,
    COMMAND_WAIT_INTRO,
    COMMAND_CONVERSATION_0020,
    COMMAND_ROOT_COMPLETE,
    COMMAND_SPAWN_NIGHTCRAWLER,
    COMMAND_WAIT_SPAWN,
    COMMAND_SPAWN_COMPLETE,
    COMMAND_WALK_NIGHTCRAWLER,
    COMMAND_WAIT_WALK,
    COMMAND_CONVERSATION_0020B,
    COMMAND_WALK_COMPLETE,
    COMMAND_CAMERA_RESET,
    COMMAND_WAIT_CLEANUP,
    COMMAND_AI_ENABLED,
    COMMAND_CONTROL_RELEASE,
    COMMAND_CLEANUP_COMPLETE
} TestCommand;

typedef enum TestMode {
    TEST_NORMAL,
    TEST_CHOICE,
    TEST_NO_PROGRESS,
    TEST_RUNAWAY
} TestMode;

typedef struct TestFiber {
    int owned;
    int active;
    size_t cursor;
    unsigned runs;
    uint32_t deadline_bits;
} TestFiber;

typedef struct Fixture {
    TestMode mode;
    TestFiber fibers[FIBER_COUNT];
    int controls_released;
    uint32_t guest_clock_bits;
    uint64_t frame_number;
    uint64_t world_updates;
    const char *events[32];
    size_t event_count;
} Fixture;

static const TestCommand root_commands[] = {
    COMMAND_CAMERA_OWNED,
    COMMAND_WAIT_INTRO,
    COMMAND_CONVERSATION_0020,
    COMMAND_ROOT_COMPLETE,
};
static const TestCommand spawn_commands[] = {
    COMMAND_SPAWN_NIGHTCRAWLER,
    COMMAND_WAIT_SPAWN,
    COMMAND_SPAWN_COMPLETE,
};
static const TestCommand walk_commands[] = {
    COMMAND_WALK_NIGHTCRAWLER,
    COMMAND_WAIT_WALK,
    COMMAND_CONVERSATION_0020B,
    COMMAND_WALK_COMPLETE,
};
static const TestCommand cleanup_commands[] = {
    COMMAND_CAMERA_RESET,
    COMMAND_WAIT_CLEANUP,
    COMMAND_AI_ENABLED,
    COMMAND_CONTROL_RELEASE,
    COMMAND_CLEANUP_COMPLETE,
};

static int failures;

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, \
                    #condition);                                               \
            failures++;                                                        \
        }                                                                      \
    } while (0)

static void fixture_init(Fixture *fixture, TestMode mode)
{
    memset(fixture, 0, sizeof *fixture);
    fixture->mode = mode;
    fixture->fibers[ROOT_FIBER].owned = 1;
    fixture->fibers[ROOT_FIBER].active = 1;
    fixture->fibers[SPAWN_FIBER].owned = 1;
    fixture->fibers[WALK_FIBER].owned = 1;
    fixture->fibers[CLEANUP_FIBER].owned = 1;
    fixture->fibers[FOREIGN_FIBER].active = 1;
    fixture->fibers[FOREIGN_FIBER].deadline_bits = 0x3dcccccd;
    fixture->guest_clock_bits = 0x42f68000;
    fixture->frame_number = UINT64_C(12017);
    fixture->world_updates = UINT64_C(8911);
}

static void record(Fixture *fixture, const char *event)
{
    if (fixture->event_count < ARRAY_COUNT(fixture->events))
        fixture->events[fixture->event_count++] = event;
}

static int active_sequence(void *context, X2CutsceneSequence *sequence)
{
    Fixture *fixture = context;
    (void)fixture;
    *sequence = 0x2002u;
    return 1;
}

static X2CutsceneControlState control_state(
    void *context, X2CutsceneSequence sequence)
{
    Fixture *fixture = context;
    if (sequence != 0x2002u) return X2_CUTSCENE_CONTROL_UNREADABLE;
    return fixture->controls_released ? X2_CUTSCENE_CONTROL_RELEASED
                                      : X2_CUTSCENE_CONTROL_LOCKED;
}

static int next_owned_fiber(void *context, X2CutsceneSequence sequence,
                            X2CutsceneFiber *fiber)
{
    Fixture *fixture = context;
    size_t i;
    if (sequence != 0x2002u) return -1;
    for (i = 0; i < FIBER_COUNT; i++) {
        if (fixture->fibers[i].owned && fixture->fibers[i].active) {
            *fiber = i;
            return 1;
        }
    }
    return 0;
}

static const TestCommand *commands_for(size_t fiber, size_t *count)
{
    switch (fiber) {
    case ROOT_FIBER:
        *count = ARRAY_COUNT(root_commands);
        return root_commands;
    case SPAWN_FIBER:
        *count = ARRAY_COUNT(spawn_commands);
        return spawn_commands;
    case WALK_FIBER:
        *count = ARRAY_COUNT(walk_commands);
        return walk_commands;
    case CLEANUP_FIBER:
        *count = ARRAY_COUNT(cleanup_commands);
        return cleanup_commands;
    default:
        *count = 0;
        return NULL;
    }
}

static X2CutsceneFiberStep step_owned_fiber(
    void *context, X2CutsceneSequence sequence, X2CutsceneFiber handle,
    X2CutsceneConversation *conversation)
{
    static const char *const command_events[] = {
        "camera_owned",       "wait_intro",       NULL,
        "root_complete",      "spawn_nightcrawler", "wait_spawn",
        "spawn_complete",     "walk_nightcrawler", "wait_walk",
        NULL,                 "walk_complete",     "camera_reset",
        "wait_cleanup",       "ai_enabled",        "control_release",
        "cleanup_complete",
    };
    Fixture *fixture = context;
    TestFiber *fiber;
    const TestCommand *commands;
    TestCommand command;
    size_t count;

    if (sequence != 0x2002u || handle >= FIBER_COUNT ||
        !fixture->fibers[handle].owned)
        return X2_CUTSCENE_FIBER_ERROR;
    fiber = &fixture->fibers[handle];
    fiber->runs++;

    if (fixture->mode == TEST_NO_PROGRESS)
        return X2_CUTSCENE_FIBER_NO_PROGRESS;
    if (fixture->mode == TEST_RUNAWAY)
        return X2_CUTSCENE_FIBER_ADVANCED;

    commands = commands_for(handle, &count);
    if (!commands || fiber->cursor >= count)
        return X2_CUTSCENE_FIBER_ERROR;
    command = commands[fiber->cursor];
    if (command == COMMAND_CONVERSATION_0020 ||
        command == COMMAND_CONVERSATION_0020B) {
        *conversation = command == COMMAND_CONVERSATION_0020 ? 0x20u : 0x20bu;
        return fixture->mode == TEST_CHOICE
                   ? X2_CUTSCENE_FIBER_CHOICE
                   : X2_CUTSCENE_FIBER_DETERMINISTIC_CONVERSATION;
    }

    record(fixture, command_events[command]);
    fiber->cursor++;
    if (command == COMMAND_ROOT_COMPLETE ||
        command == COMMAND_SPAWN_COMPLETE ||
        command == COMMAND_WALK_COMPLETE ||
        command == COMMAND_CLEANUP_COMPLETE) {
        fiber->active = 0;
        if (command == COMMAND_SPAWN_COMPLETE)
            fixture->fibers[WALK_FIBER].active = 1;
        return X2_CUTSCENE_FIBER_COMPLETED;
    }
    if (command == COMMAND_CONTROL_RELEASE)
        fixture->controls_released = 1;
    return X2_CUTSCENE_FIBER_ADVANCED;
}

static int play_deterministic_conversation(
    void *context, X2CutsceneSequence sequence,
    X2CutsceneConversation conversation)
{
    Fixture *fixture = context;
    TestFiber *fiber;
    if (sequence != 0x2002u) return 0;
    if (conversation == 0x20u) {
        fiber = &fixture->fibers[ROOT_FIBER];
        record(fixture, "conversation_0020");
        fixture->fibers[SPAWN_FIBER].active = 1;
    } else if (conversation == 0x20bu) {
        fiber = &fixture->fibers[WALK_FIBER];
        record(fixture, "conversation_0020b");
        fixture->fibers[CLEANUP_FIBER].active = 1;
    } else {
        return 0;
    }
    fiber->cursor++;
    return 1;
}

static const X2CutscenePlayerOps operations = {
    active_sequence,
    control_state,
    next_owned_fiber,
    step_owned_fiber,
    play_deterministic_conversation,
};

static void test_complete_sequence(void)
{
    static const char *const expected[] = {
        "camera_owned",       "wait_intro",       "conversation_0020",
        "root_complete",      "spawn_nightcrawler", "wait_spawn",
        "spawn_complete",     "walk_nightcrawler", "wait_walk",
        "conversation_0020b", "walk_complete",     "camera_reset",
        "wait_cleanup",       "ai_enabled",        "control_release",
    };
    Fixture fixture;
    TestFiber foreign_before;
    X2CutscenePlayerPolicy policy = {0};
    uint32_t clock_before;
    uint64_t frame_before, world_before;
    size_t i;

    fixture_init(&fixture, TEST_NORMAL);
    foreign_before = fixture.fibers[FOREIGN_FIBER];
    clock_before = fixture.guest_clock_bits;
    frame_before = fixture.frame_number;
    world_before = fixture.world_updates;

    CHECK(x2_cutscene_player_finish(&policy, &operations, &fixture) ==
          X2_CUTSCENE_PLAYER_COMPLETED);
    CHECK(policy.requests == 1u);
    CHECK(policy.invocations == 1u);
    CHECK(policy.completed == 1u);
    CHECK(policy.authored_steps == 13u);
    CHECK(policy.conversation_payloads == 2u);
    CHECK(fixture.controls_released);
    CHECK(fixture.fibers[CLEANUP_FIBER].active);
    CHECK(fixture.fibers[CLEANUP_FIBER].cursor == 4u);
    CHECK(fixture.event_count == ARRAY_COUNT(expected));
    for (i = 0; i < ARRAY_COUNT(expected) && i < fixture.event_count; i++)
        CHECK(strcmp(fixture.events[i], expected[i]) == 0);

    CHECK(memcmp(&fixture.fibers[FOREIGN_FIBER], &foreign_before,
                 sizeof foreign_before) == 0);
    CHECK(fixture.guest_clock_bits == clock_before);
    CHECK(fixture.frame_number == frame_before);
    CHECK(fixture.world_updates == world_before);
}

static void test_refusals(void)
{
    Fixture fixture;
    X2CutscenePlayerPolicy policy;

    fixture_init(&fixture, TEST_CHOICE);
    memset(&policy, 0, sizeof policy);
    CHECK(x2_cutscene_player_finish(&policy, &operations, &fixture) ==
          X2_CUTSCENE_PLAYER_BLOCKED_CHOICE);
    CHECK(policy.requests == 1u && policy.invocations == 1u);
    CHECK(policy.blocked_choices == 1u);
    CHECK(policy.conversation_payloads == 0u);
    CHECK(!fixture.controls_released);

    fixture_init(&fixture, TEST_NO_PROGRESS);
    memset(&policy, 0, sizeof policy);
    CHECK(x2_cutscene_player_finish(&policy, &operations, &fixture) ==
          X2_CUTSCENE_PLAYER_NO_PROGRESS);
    CHECK(policy.requests == 1u && policy.invocations == 1u);
    CHECK(policy.no_progress == 1u);
    CHECK(fixture.fibers[ROOT_FIBER].runs == 1u);

    fixture_init(&fixture, TEST_RUNAWAY);
    memset(&policy, 0, sizeof policy);
    policy.step_limit = 3u;
    CHECK(x2_cutscene_player_finish(&policy, &operations, &fixture) ==
          X2_CUTSCENE_PLAYER_RUNAWAY);
    CHECK(policy.requests == 1u && policy.invocations == 1u);
    CHECK(policy.runaways == 1u);
    CHECK(policy.authored_steps == 3u);
    CHECK(fixture.fibers[ROOT_FIBER].runs == 3u);
}

static void test_context_inheritance(void)
{
    CHECK(!x2_cutscene_player_inherits_context(0, 1, 1, 1));
    CHECK(!x2_cutscene_player_inherits_context(1, 0, 0, 0));
    CHECK(x2_cutscene_player_inherits_context(1, 1, 0, 0));
    CHECK(x2_cutscene_player_inherits_context(1, 0, 1, 0));
    CHECK(x2_cutscene_player_inherits_context(1, 0, 0, 1));
}

int main(void)
{
    test_complete_sequence();
    test_refusals();
    test_context_inheritance();
    return failures ? 1 : 0;
}
