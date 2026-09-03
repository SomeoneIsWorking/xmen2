/*
 * Native owner of one-step gameplay cutscene completion.
 *
 * The retail owner is the BehavEd player, not the conversation manager:
 * CPythonGameInterface::update (0x004a00d0) forwards to the timed fiber pump
 * at 0x004d9640.  `lockControls(-1)` identifies an authored sequence that
 * owns player control.  Contexts allocated by that context (and scripts
 * launched by its deterministic conversation payloads) inherit the sequence.
 *
 * A skip runs owned work through the ported player until authored control
 * release, without advancing a guest clock, frame, world tick, or deadline.
 */
#include "cutscene_player.h"

#include "../input/gameplay_control.h"
#include "cutscene_control_clock.h"
#include "behaved_player.h"
#include "cutscene_dialogue.h"
#include "conversation_player.h"
#include "cutscene_event_player.h"
#include "cutscene_player_policy.h"
#include "gpu_device.h"
#include "x86rt.h"
#include "x86rt_native.h"
#include <stdint.h>
#include <string.h>
#include "guest_body.h"
#define EXE_PREFERRED           0x00400000u
#define EXE_RVA(va)             ((uint32_t)(va) - EXE_PREFERRED)
#define CURRENT_CONTEXT_RVA     EXE_RVA(0x00787730u)
#define FN_INPUT                0x005d8920u
#define INPUT_ACTION_MASK_SLOT  0x140u
#define CINEMATIC_SKIP_ACTION   20u
#define OWNED_CONTEXT_LIMIT     30u
#define CONVERSATION_FIBER      ((X2CutsceneFiber)UINTPTR_MAX)
#define EVENT_FIBER_BASE        ((X2CutsceneFiber)UINT64_C(0x100000000))

typedef struct CutscenePlayerRuntime {
    X2CutscenePlayerPolicy policy;
    CutsceneEventOwnershipWindow events;
    uint32_t clock;
    uint32_t owned[OWNED_CONTEXT_LIMIT];
    unsigned owned_count;
    unsigned sequence;
    unsigned active : 1;
    unsigned finishing : 1;
    unsigned skip_down : 1;
    unsigned release_pending : 1;
    unsigned event_refused : 1;
    unsigned long starts;
    unsigned long releases;
    unsigned long private_releases;
    unsigned long allocations;
    unsigned long inherited;
    unsigned long freed;
    unsigned long input_polls;
    unsigned long input_edges;
    unsigned long same_frame;
    unsigned long same_guest_time;
    unsigned long event_faults_at_start;
    unsigned long behaved_steps;
    unsigned long event_steps;
    uint32_t last_event_target;
    uint32_t last_event_descriptor;
    unsigned long result[6];
} CutscenePlayerRuntime;

static CutscenePlayerRuntime g_player;


static uint32_t current_context(void);

static uint32_t exe_base(void)
{
    X86Module *module;
    for (module = x86_modules(); module; module = module->next)
        if (module->preferred == EXE_PREFERRED && module->base &&
            *module->base)
            return *module->base;
    return 0;
}

static uint32_t linked(uint32_t preferred)
{
    uint32_t base = exe_base();

    return base ? base + EXE_RVA(preferred) : 0;
}

static int owned_index(uint32_t context)
{
    unsigned i;

    for (i = 0; i < g_player.owned_count; i++)
        if (g_player.owned[i] == context) return (int)i;
    return -1;
}

static int owns_context(uint32_t context, void *opaque)
{
    (void)opaque;
    return context && owned_index(context) >= 0;
}

static int owns_inserting_context(const CPU *cpu, void *opaque)
{
    (void)cpu;
    return owns_context(current_context(), opaque);
}

static void own(uint32_t context)
{
    if (!context || owned_index(context) >= 0) return;
    if (g_player.owned_count >= OWNED_CONTEXT_LIMIT) return;
    g_player.owned[g_player.owned_count++] = context;
    g_player.inherited++;
}

static void disown(uint32_t context)
{
    int index = owned_index(context);

    if (index < 0) return;
    g_player.owned_count--;
    g_player.owned[index] = g_player.owned[g_player.owned_count];
    g_player.owned[g_player.owned_count] = 0;
    g_player.freed++;
}

static uint32_t current_context(void)
{
    uint32_t base = exe_base(), context = 0;

    if (base) (void)x86_peek32(base + CURRENT_CONTEXT_RVA, &context);
    return context;
}

static void begin_sequence(uint32_t clock, uint32_t context)
{
    memset(g_player.owned, 0, sizeof g_player.owned);
    g_player.owned_count = 0;
    g_player.clock = clock;
    g_player.sequence++;
    if (!g_player.sequence) g_player.sequence++;
    g_player.active = 1;
    g_player.release_pending = 0;
    g_player.event_refused = 0;
    g_player.event_faults_at_start =
        cutscene_event_player_insertion_faults();
    g_player.starts++;
    own(context);
    memset(&g_player.events, 0, sizeof g_player.events);
    if (cutscene_event_player_window_begin(&g_player.events) < 0)
        g_player.event_refused = 1;
    if (cutscene_event_player_watch_insertions(
            &g_player.events, owns_inserting_context, NULL) < 0)
        g_player.event_refused = 1;
}

static int claim_events(void)
{
    int claimed;

    if (g_player.event_refused ||
        cutscene_event_player_insertion_faults() !=
            g_player.event_faults_at_start)
        return 0;
    if (!g_player.events.active) {
        claimed = cutscene_event_player_window_begin(&g_player.events);
        if (claimed <= 0) return claimed == 0;
    }
    claimed = cutscene_event_player_window_claim_new(&g_player.events);
    if (claimed < 0) g_player.event_refused = 1;
    return claimed >= 0;
}

static int call_action_mask(CPU *cpu, uint32_t *mask)
{
    CPU call;
    uint32_t input, vtable, function;

    if (!cpu || !mask) return 0;
    call = *cpu;
    x86_guest_call(&call, linked(FN_INPUT));
    input = call.eax;
    if (!input || !x86_peek32(input, &vtable) ||
        !x86_peek32(vtable + INPUT_ACTION_MASK_SLOT, &function) || !function)
        return 0;
    call = *cpu;
    call.ecx = input;
    x86_guest_call_args(&call, function, 0u);
    *mask = call.eax;
    return 1;
}

static int active_sequence(void *context, X2CutsceneSequence *sequence)
{
    (void)context;
    if (!g_player.active) return 0;
    *sequence = g_player.sequence;
    return 1;
}

static X2CutsceneControlState control_state(
    void *context, X2CutsceneSequence sequence)
{
    (void)context;
    if (!g_player.active || sequence != g_player.sequence)
        return X2_CUTSCENE_CONTROL_UNREADABLE;
    switch (cutscene_control_clock_state(g_player.clock)) {
    case kX2CutsceneClockLocked:
        return X2_CUTSCENE_CONTROL_LOCKED;
    case kX2CutsceneClockReleased:
        return X2_CUTSCENE_CONTROL_RELEASED;
    default:
        return X2_CUTSCENE_CONTROL_UNREADABLE;
    }
}

static void retire_released_sequence(void)
{
    if (!g_player.active || !g_player.release_pending ||
        g_player.owned_count ||
        cutscene_control_clock_state(g_player.clock) !=
            kX2CutsceneClockReleased)
        return;
    g_player.active = 0;
    g_player.release_pending = 0;
    cutscene_event_player_unwatch_insertions(&g_player.events);
}

static int next_owned_fiber(void *context, X2CutsceneSequence sequence,
                            X2CutsceneFiber *fiber)
{
    CPU *cpu = context;
    ConversationPlayerState conversation;
    uint32_t selected = 0;
    int available;

    if (!g_player.active || sequence != g_player.sequence) return -1;
    if (!claim_events()) return -1;
    if (g_player.events.active) {
        available = cutscene_event_player_next_owned(&g_player.events,
                                                     &selected);
        if (available > 0) {
            *fiber = EVENT_FIBER_BASE + selected;
            return 1;
        }
        if (available < 0) return -1;
    }
    conversation = conversation_player_state(cpu);
    if (conversation == CONVERSATION_PLAYER_DETERMINISTIC ||
        conversation == CONVERSATION_PLAYER_CHOICE) {
        *fiber = CONVERSATION_FIBER;
        return 1;
    }
    if (conversation == CONVERSATION_PLAYER_UNREADABLE) return -1;
    available = behaved_player_next_owned(cpu, owns_context, NULL, &selected);
    if (available > 0) *fiber = selected;
    return available;
}

static X2CutsceneFiberStep step_owned_fiber(
    void *context, X2CutsceneSequence sequence, X2CutsceneFiber fiber,
    X2CutsceneConversation *conversation)
{
    BehavedPlayerStep step;

    if (!g_player.active || sequence != g_player.sequence)
        return X2_CUTSCENE_FIBER_ERROR;
    if (fiber >= EVENT_FIBER_BASE &&
        fiber < EVENT_FIBER_BASE + CUTSCENE_EVENT_PLAYER_CAPACITY) {
        uint32_t slot = (uint32_t)(fiber - EVENT_FIBER_BASE);
        uint32_t record = g_player.events.owner + slot * 0x18u;
        (void)x86_peek32(record, &g_player.last_event_target);
        (void)x86_peek32(record + 4u, &g_player.last_event_descriptor);
        CutsceneEventPlayerStep event = cutscene_event_player_step_owned_slot(
            context, &g_player.events, slot);
        g_player.event_steps++;
        if (event == CUTSCENE_EVENT_PLAYER_STEP_RAN)
            return X2_CUTSCENE_FIBER_ADVANCED;
        return event == CUTSCENE_EVENT_PLAYER_STEP_NONE
            ? X2_CUTSCENE_FIBER_NO_PROGRESS
            : X2_CUTSCENE_FIBER_ERROR;
    }
    if (fiber == CONVERSATION_FIBER) {
        ConversationPlayerState state = conversation_player_state(context);
        *conversation = 1;
        if (state == CONVERSATION_PLAYER_DETERMINISTIC)
            return X2_CUTSCENE_FIBER_DETERMINISTIC_CONVERSATION;
        if (state == CONVERSATION_PLAYER_CHOICE)
            return X2_CUTSCENE_FIBER_CHOICE;
        return X2_CUTSCENE_FIBER_NO_PROGRESS;
    }
    step = behaved_player_step_context(context, (uint32_t)fiber);
    g_player.behaved_steps++;
    if (!claim_events()) return X2_CUTSCENE_FIBER_ERROR;
    switch (step) {
    case BEHAVED_PLAYER_STEP_RAN:
        return X2_CUTSCENE_FIBER_ADVANCED;
    case BEHAVED_PLAYER_STEP_COMPLETED:
        return X2_CUTSCENE_FIBER_COMPLETED;
    case BEHAVED_PLAYER_STEP_NONE:
        return X2_CUTSCENE_FIBER_NO_PROGRESS;
    case BEHAVED_PLAYER_STEP_REFUSED:
    default:
        return X2_CUTSCENE_FIBER_ERROR;
    }
}

static int play_conversation(void *context, X2CutsceneSequence sequence,
                             X2CutsceneConversation conversation)
{
    int advanced;

    if (!conversation || !g_player.active || sequence != g_player.sequence)
        return 0;
    advanced = cutscene_dialogue_advance(context);
    return advanced && claim_events();
}

static X2CutscenePlayerResult finish(CPU *cpu)
{
    static const X2CutscenePlayerOps ops = {
        active_sequence,
        control_state,
        next_owned_fiber,
        step_owned_fiber,
        play_conversation,
    };
    X2CutscenePlayerResult result;
    unsigned long frame_before = gpu_frames_presented();
    uint32_t time_before = 0, time_after = 1;

    (void)cutscene_control_clock_now_bits(g_player.clock, &time_before);
    cutscene_dialogue_skip_begin();
    g_player.finishing = 1;
    result = x2_cutscene_player_finish(&g_player.policy, &ops, cpu);
    g_player.finishing = 0;
    cutscene_dialogue_skip_end(cpu);
    (void)cutscene_control_clock_now_bits(g_player.clock, &time_after);
    if (gpu_frames_presented() == frame_before) g_player.same_frame++;
    if (time_after == time_before) g_player.same_guest_time++;
    if ((unsigned)result < sizeof g_player.result / sizeof g_player.result[0])
        g_player.result[result]++;
    if (result == X2_CUTSCENE_PLAYER_COMPLETED) {
        g_player.active = 0;
        g_player.release_pending = 0;
        cutscene_event_player_unwatch_insertions(&g_player.events);
        g_player.owned_count = 0;
        memset(g_player.owned, 0, sizeof g_player.owned);
    }
    return result;
}

void x2_override_00469130(CPU *cpu)
{
    uint32_t bits = RD32(cpu->esp + 4u);
    float seconds = cutscene_control_clock_seconds(bits);
    uint32_t context = current_context();

    if (seconds < 0.0f && context) {
        if (!g_player.active) begin_sequence(cpu->ecx, context);
        g_player.release_pending = 0;
    }
    x86_guest_body(cpu, "XMen2.exe", 0x00469130u);
    if (seconds >= 0.0f && g_player.active) {
        g_player.release_pending = 1;
        g_player.releases++;
        if (g_player.finishing &&
            cutscene_control_clock_release_now(g_player.clock))
            g_player.private_releases++;
    }
}

void x2_override_004d8700(CPU *cpu)
{
    x86_guest_body(cpu, "XMen2.exe", 0x004d8700u);
    g_player.allocations++;
    if (cpu->eax && x2_cutscene_player_inherits_context(
            g_player.active, owns_context(current_context(), NULL),
            cutscene_event_player_executing_owned(),
            cutscene_dialogue_payload_active()))
        own(cpu->eax);
}

void x2_override_004d7c10(CPU *cpu)
{
    uint32_t vm = 0, context = 0;
    uint32_t index = RD32(cpu->esp + 4u);
    CPU call = *cpu;

    x86_guest_call(&call, linked(0x004d8770u));
    vm = call.eax;
    if (vm && index < OWNED_CONTEXT_LIMIT)
        context = vm + 0x2f2f4u + index * 0x5c4u;
    x86_guest_body(cpu, "XMen2.exe", 0x004d7c10u);
    disown(context);
}

void x2_override_004a00d0(CPU *cpu)
{
    uint32_t mask = 0;
    int down = 0;

    x86_guest_body(cpu, "XMen2.exe", 0x004a00d0u);
    retire_released_sequence();
    /* Publish the lock to the one owner of "does the player control a
       character": this override runs every input poll, cutscene or not, so
       the RELEASE is published as reliably as the acquisition. */
    x2_gameplay_control_set_cutscene_locked(
        g_player.active &&
        control_state(cpu, g_player.sequence) == X2_CUTSCENE_CONTROL_LOCKED);
    g_player.input_polls++;
    if (call_action_mask(cpu, &mask))
        down = !!(mask & (1u << CINEMATIC_SKIP_ACTION));
    if (down && !g_player.skip_down) {
        g_player.input_edges++;
        (void)finish(cpu);
    }
    g_player.skip_down = (unsigned)down;
}

void cutscene_player_snapshot(CPU *cpu, CutscenePlayerSnapshot *out)
{
    X2CutsceneControlState controls = g_player.active
        ? control_state(cpu, g_player.sequence)
        : X2_CUTSCENE_CONTROL_RELEASED;

    if (!out) return;
    memset(out, 0, sizeof *out);
    out->sequence = g_player.sequence;
    out->active = g_player.active;
    out->owned_contexts = g_player.owned_count;
    out->event_window_active = g_player.events.active;
    out->event_refused = g_player.event_refused;
    out->event_owner = g_player.events.owner;
    out->control_state = (unsigned)(controls + 1);
    out->requests = g_player.policy.requests;
    out->invocations = g_player.policy.invocations;
    out->completions = g_player.policy.completed;
    out->input_polls = g_player.input_polls;
    out->input_edges = g_player.input_edges;
    out->authored_steps = g_player.policy.authored_steps;
    out->conversation_payloads = g_player.policy.conversation_payloads;
    out->behaved_steps = g_player.behaved_steps;
    out->event_steps = g_player.event_steps;
    out->last_event_target = g_player.last_event_target;
    out->last_event_descriptor = g_player.last_event_descriptor;
    out->allocations = g_player.allocations;
    out->inherited = g_player.inherited;
    out->freed = g_player.freed;
    out->releases = g_player.releases;
    out->private_releases = g_player.private_releases;
    out->same_frame = g_player.same_frame;
    out->same_guest_time = g_player.same_guest_time;
    out->event_insertion_faults =
        cutscene_event_player_insertion_faults();
    memcpy(out->results, g_player.result, sizeof out->results);
}

int cutscene_player_silences_current_context(uint32_t *context)
{
    *context = current_context();
    return g_player.finishing && owns_context(*context, NULL);
}

__attribute__((constructor))
static void x2_cutscene_player_register_overrides(void)
{
    x86_register_override("XMen2.exe", 0x00469130u,
                          x2_override_00469130);
    x86_register_override("XMen2.exe", 0x004a00d0u,
                          x2_override_004a00d0);
    x86_register_override("XMen2.exe", 0x004d7c10u,
                          x2_override_004d7c10);
    x86_register_override("XMen2.exe", 0x004d8700u,
                          x2_override_004d8700);
}
