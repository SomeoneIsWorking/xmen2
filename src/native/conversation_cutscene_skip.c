/* Escape/Start skipping for authored, in-engine conversation cutscenes.
 *
 * These are not `cinematicStart` sequences. Shipped gameplay scripts express
 * them as a deterministic conversation chain, often with locked controls or
 * `noReturnToGameCamAtEnd`, then let response and chosen scripts perform every
 * camera reset, control unlock, spawn, quest update, and zone transition.
 * Skipping therefore has two halves:
 *
 *   1. Dialogue records advance per frame through the retail chooseResponse
 *      path (the conversation update override). It never clears presentation
 *      flags and it stops at a real branch.
 *   2. The scripted time BETWEEN records -- the tutorial's teleport cutaway is
 *      playanim + waittimed(2.0) + fx + waittimed(0.5), and the cleanup script
 *      holds another 2.5s of waits before it unlocks controls -- is shortened
 *      by clamping the script scheduler's wait deadlines to a small floor
 *      while the skip latch is armed. The floor is load-bearing: with a zero
 *      floor (82bdf13) the next startConversation fired inside the ending
 *      conversation's unwind and reproduced the issue #83 no-line signature.
 *      Retail's own waits are what pace the manager between records, so the
 *      floor keeps the manager's cadence while removing the stall.
 *
 * Both halves are scoped to the armed sequence: the wait clamp claims the one
 * script scheduler context that waits while the latch holds, refuses foreign
 * contexts outright, and times itself out rather than ever shortening a wait
 * outside an authored skip.
 */
#include "conversation_cutscene_skip.h"

#include "conversation_skip_policy.h"
#include "guest_clock.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define EXE_PREFERRED       0x00400000u
#define EXE_RVA(va)         ((uint32_t)(va) - EXE_PREFERRED)
#define CONV_SINGLETON_RVA  EXE_RVA(0x00717aacu)
#define CLOCK_OBJECT_RVA    EXE_RVA(0x00729960u)
#define FN_SLOT_OF_RVA      EXE_RVA(0x00456440u)
#define SCRIPT_CONTEXT_RVA  EXE_RVA(0x00787730u)
#define FN_SCHEDULE_WAIT    0x004d6a00u  /* scheduler insert: (deadline, ctx) */

#define CV_KEY_A            0x004b0u
#define CV_KEY_B            0x00008u
#define CV_RESP_IDS         0x004c0u
#define CV_RESP_COUNT       0x004e0u
#define CV_ASSET_TABLE      0x0032cu
#define CV_FLAGS            0x21b24u
#define CVF_VISIBLE         0x02u
#define ASSET_NO_RETURN_CAM 0x00118u
#define CLOCK_NOW           0x003e8u
#define CLOCK_CONTROL_LIMIT 0x003f4u
#define INPUT_ACTION_MASK    0x00138u
#define CUTSCENE_SKIP_ACTION 20u
#define SLOT_NONE           0x3fffffffu
#define RESPONSE_NONE       0xffffffffu
#define RESPONSE_SLOTS      8u

typedef struct ConversationCutsceneSnapshot {
    int readable;
    int visible;
    int no_return_camera;
    int controls_locked;
    int authored;
    unsigned responses;
    ConversationSkipResponse response;
} ConversationCutsceneSnapshot;

static ConversationSkipPolicy g_policy;
static unsigned long g_action_checks, g_action_down;

/* The wait-floor scope. While a skip sequence is armed -- by an Escape at the
 * authored boundary or by the boot-Continue resume -- the player has asked
 * for this scene to fast-forward, so EVERY script-scheduler wait clamps to
 * the floor. A cutscene is a CHAIN of script contexts (tutorial1 spawns
 * nightcrawler_spawn spawns nightcrawler_walk spawns the cleanup), so a
 * single owner-context claim cannot cover it: the first live run shortened
 * 1 wait of 10 and refused the chain's own scripts as foreign. The latch's
 * arm/retire boundaries and the runaway timeout are the scope instead; the
 * floor keeps the conversation manager's between-records cadence, which is
 * what the zero-floor 82bdf13 attempt broke (issue #83 signature). */
#define WAIT_FLOOR_S        0.10f
#define WAIT_SCOPE_TIMEOUT_S 30.0

static struct {
    double armed_at_s;
    int exhausted;
} g_wait_scope;
static unsigned long g_waits_seen, g_waits_shortened, g_waits_unowned;

void fn_XMen2_004d6a00(CPU *C);

static uint32_t exe_base(void)
{
    X86Module *module;
    for (module = x86_modules(); module; module = module->next)
        if (module->preferred == EXE_PREFERRED && *module->base)
            return *module->base;
    return 0;
}

static int peek8(uint32_t address, uint8_t *out)
{
    uint32_t word;
    unsigned shift = (address & 3u) * 8u;
    if (!x86_peek32(address & ~3u, &word)) return 0;
    *out = (uint8_t)(word >> shift);
    return 1;
}

static int peek_float(uint32_t address, float *out)
{
    uint32_t bits;
    if (!x86_peek32(address, &bits)) return 0;
    memcpy(out, &bits, sizeof bits);
    return 1;
}

static uint32_t current_slot(CPU *cpu, uint32_t base, uint32_t self)
{
    CPU call;
    uint32_t args[2];
    uint8_t flags;

    if (!cpu || !base || !self) return SLOT_NONE;
    /* The retail lookup stricmps the NAME FIELD of every table node it
       walks, and a manager that has not started a conversation yet still
       holds NULL names -- executing the lookup from a poll during level
       construction faulted inside _stricmp. Production never does it: the
       update override reaches FN_SLOT_OF only after its visible gate, so
       this diagnostic asks for the same state before asking at all. */
    if (!peek8(self + CV_FLAGS, &flags) || !(flags & CVF_VISIBLE))
        return SLOT_NONE;
    args[0] = RD32(self + CV_KEY_A);
    args[1] = RD32(self + CV_KEY_B);
    call = *cpu;
    call.esp -= sizeof args;
    WR32(call.esp, args[0]);
    WR32(call.esp + 4u, args[1]);
    call.ecx = self + 4u;
    x86_guest_call_args(&call, base + FN_SLOT_OF_RVA, sizeof args);
    return call.eax;
}

static ConversationCutsceneSnapshot snapshot(uint32_t base, uint32_t self,
                                              uint32_t slot)
{
    ConversationCutsceneSnapshot out = {0};
    uint32_t asset = 0, count = 0, id;
    uint8_t flags = 0, no_return = 0;
    float now = 0.0f, limit = 0.0f;
    unsigned i;

    out.response = CONVERSATION_SKIP_RESPONSE_UNREADABLE;
    if (!base || !self || !peek8(self + CV_FLAGS, &flags) ||
        !peek_float(base + CLOCK_OBJECT_RVA + CLOCK_NOW, &now) ||
        !peek_float(base + CLOCK_OBJECT_RVA + CLOCK_CONTROL_LIMIT, &limit))
        return out;

    out.readable = 1;
    out.visible = !!(flags & CVF_VISIBLE);
    out.controls_locked = limit < 0.0f || limit > now;
    if (slot != SLOT_NONE &&
        x86_peek32(self + CV_ASSET_TABLE + slot * 4u, &asset) && asset &&
        peek8(asset + ASSET_NO_RETURN_CAM, &no_return))
        out.no_return_camera = !!no_return;
    out.authored = conversation_skip_policy_is_authored(
        out.no_return_camera, out.controls_locked);
    out.response = CONVERSATION_SKIP_RESPONSE_WAITING;
    if (!out.visible) return out;
    if (slot == SLOT_NONE || !asset ||
        !x86_peek32(self + CV_RESP_COUNT, &count)) {
        out.readable = 0;
        out.response = CONVERSATION_SKIP_RESPONSE_UNREADABLE;
        return out;
    }
    if (count > RESPONSE_SLOTS) {
        out.readable = 0;
        out.response = CONVERSATION_SKIP_RESPONSE_UNREADABLE;
        return out;
    }
    for (i = 0; i < count; i++) {
        if (!x86_peek32(self + CV_RESP_IDS + i * 4u, &id)) {
            out.readable = 0;
            out.response = CONVERSATION_SKIP_RESPONSE_UNREADABLE;
            return out;
        }
        if (id != RESPONSE_NONE) out.responses++;
    }
    if (out.responses == 1u)
        out.response = CONVERSATION_SKIP_RESPONSE_DETERMINISTIC;
    else if (out.responses > 1u)
        out.response = CONVERSATION_SKIP_RESPONSE_CHOICE;
    return out;
}

ConversationSkipResponse conversation_cutscene_skip_response(
    CPU *cpu, uint32_t self)
{
    uint32_t base = exe_base();
    return snapshot(base, self, current_slot(cpu, base, self)).response;
}

static int action20_down(CPU *cpu, uint32_t input)
{
    CPU call = *cpu;
    uint32_t vtable = RD32(input), action = CUTSCENE_SKIP_ACTION;
    int down;

    call.esp -= 4u;
    WR32(call.esp, action);
    call.ecx = input;
    x86_guest_call_args(&call, RD32(vtable + INPUT_ACTION_MASK), 4u);
    down = (uint8_t)call.eax;
    g_action_checks++;
    if (down) g_action_down++;
    return down;
}

int conversation_cutscene_skip_controls_locked(void)
{
    uint32_t base = exe_base();
    float now = 0.0f, limit = 0.0f;

    if (!base ||
        !peek_float(base + CLOCK_OBJECT_RVA + CLOCK_NOW, &now) ||
        !peek_float(base + CLOCK_OBJECT_RVA + CLOCK_CONTROL_LIMIT, &limit))
        return 0;
    return limit < 0.0f || limit > now;
}

int conversation_cutscene_skip_should_advance(CPU *cpu, uint32_t self,
                                               uint32_t slot, uint32_t input)
{
    ConversationCutsceneSnapshot state = snapshot(exe_base(), self, slot);
    int pressed = action20_down(cpu, input);
    ConversationSkipDecision decision = conversation_skip_policy_update(
        &g_policy, state.visible, state.readable && state.authored,
        state.controls_locked, pressed, state.response);
    return decision == CONVERSATION_SKIP_ADVANCE;
}

void conversation_cutscene_skip_observe_inactive(struct CPU *cpu,
                                                 uint32_t self,
                                                 uint32_t input)
{
    ConversationCutsceneSnapshot state = snapshot(exe_base(), self, SLOT_NONE);

    /* An unreadable clock cannot prove cleanup restored control, so retain the
       latch until production state becomes readable again. */
    if (!state.readable) return;
    /* The authored sequence owns input whenever the control lock holds, and
       that includes the camera-only stretches before, between and after
       conversation records -- the opening pan of the tutorial's intro plays
       for seconds before its first line exists. An Escape pressed there must
       arm the same latch a press on a visible line arms, or the user skips
       one record and watches the rest of the sequence they asked to skip. */
    if (cpu && input && !g_policy.active && state.controls_locked &&
        action20_down(cpu, input)) {
        g_policy.requests++;
        g_policy.active = 1;
    }
    (void)conversation_skip_policy_update(
        &g_policy, 0, 0, state.controls_locked, 0,
        CONVERSATION_SKIP_RESPONSE_WAITING);
}

static void wait_scope_reset(void)
{
    g_wait_scope.armed_at_s = 0.0;
    g_wait_scope.exhausted = 0;
}

static int wait_scope_timed_out(double now_s)
{
    if (!g_wait_scope.armed_at_s) return 0;
    if (now_s - g_wait_scope.armed_at_s < WAIT_SCOPE_TIMEOUT_S) return 0;
    g_wait_scope.exhausted = 1;
    return 1;
}

/* May this wait be shortened? Only while a skip sequence is armed, and only
 * until the runaway timeout -- the floor is not a permanent change to the
 * script engine's pacing. */
static int wait_scope_allows(uint32_t context)
{
    int sequence_active = g_policy.active;
    double wall_now = guest_clock_elapsed_s();

    (void)context;
    if (!sequence_active) {
        if (g_wait_scope.armed_at_s) wait_scope_reset();
        return 0;
    }
    if (g_wait_scope.exhausted || wait_scope_timed_out(wall_now)) return 0;
    if (!g_wait_scope.armed_at_s) g_wait_scope.armed_at_s = wall_now;
    return 1;
}

/* The clamp itself, pure: never extend, never go below the floor. */
static float wait_deadline_clamped(float now, float authored_deadline)
{
    float floor_deadline = now + WAIT_FLOOR_S;
    return authored_deadline < floor_deadline ? authored_deadline
                                              : floor_deadline;
}

void x2_override_004d6a00(CPU *C)
{
    uint32_t base = exe_base();
    float now = 0.0f, deadline;

    g_waits_seen++;
    if (!base || !peek_float(base + CLOCK_OBJECT_RVA + CLOCK_NOW, &now) ||
        !wait_scope_allows(0)) {
        g_waits_unowned++;
        fn_XMen2_004d6a00(C);
        return;
    }
    deadline = wait_deadline_clamped(now, *(float *)(uintptr_t)(C->esp + 4u));
    WR32(C->esp + 4u, *(uint32_t *)&deadline);
    g_waits_shortened++;
    fn_XMen2_004d6a00(C);
}

size_t conversation_cutscene_skip_status(char *out, size_t size)
{
    int wrote;
    if (!out || !size) return 0;
    wrote = snprintf(
        out, size,
        "authored skip %s: %lu action check(s), %lu DOWN; %u request(s), "
        "%u retail response advance(s), %u blocked, %u ignored, %u "
        "completed after control unlock; script waits %lu/%lu floor-limited "
        "(%.2fs), %lu unowned, scope %s",
        g_policy.active ? "ACTIVE" : "idle", g_action_checks,
        g_action_down, g_policy.requests, g_policy.advances,
        g_policy.blocked, g_policy.ignored, g_policy.completed,
        g_waits_shortened, g_waits_seen, (double)WAIT_FLOOR_S,
        g_waits_unowned,
        g_wait_scope.exhausted ? "EXPIRED"
            : g_wait_scope.armed_at_s ? "armed" : "idle");
    if (wrote < 0) return 0;
    if ((size_t)wrote >= size) return size - 1u;
    return (size_t)wrote;
}

void conversation_cutscene_skip_report(void)
{
    char status[512];
    conversation_cutscene_skip_status(status, sizeof status);
    printf("        %s\n", status);
}

static void append(char *out, size_t size, size_t *at, const char *fmt, ...)
    __attribute__((format(printf, 4, 5)));

static void append(char *out, size_t size, size_t *at, const char *fmt, ...)
{
    va_list args;
    int wrote;
    if (*at >= size) return;
    va_start(args, fmt);
    wrote = vsnprintf(out + *at, size - *at, fmt, args);
    va_end(args);
    if (wrote > 0)
        *at += (size_t)wrote > size - *at ? size - *at : (size_t)wrote;
}

size_t conversation_cutscene_skip_probe(CPU *cpu, char *out, size_t size)
{    static const char *const response_name[] = {
        "waiting", "deterministic", "choice", "UNREADABLE"
    };
    ConversationCutsceneSnapshot state = {0};
    uint32_t base = exe_base(), self = 0, slot = SLOT_NONE;
    size_t at = 0;

    if (!out || !size) return 0;
    if (base && x86_peek32(base + CONV_SINGLETON_RVA, &self) && self) {
        slot = current_slot(cpu, base, self);
        state = snapshot(base, self, slot);
    }
    append(out, size, &at,
           "  authored conversation: %s; visible %s; camera-owned %s; "
           "controls-locked %s; responses %u (%s)\n",
           state.readable ? (state.authored ? "yes" : "no") : "UNREADABLE",
           state.visible ? "yes" : "no",
           state.no_return_camera ? "yes" : "no",
           state.controls_locked ? "yes" : "no", state.responses,
           response_name[state.response]);
    append(out, size, &at, "  ");
    at += conversation_cutscene_skip_status(out + at, size - at);
    append(out, size, &at, "\n");
    return at;
}

__attribute__((constructor))
static void x2_conversation_cutscene_skip_register_overrides(void)
{
    x86_register_override("XMen2.exe", FN_SCHEDULE_WAIT,
                          x2_override_004d6a00);
}
