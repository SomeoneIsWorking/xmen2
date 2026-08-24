/* Escape/Start skipping for authored, in-engine conversation cutscenes.
 *
 * These are not `cinematicStart` sequences. Shipped gameplay scripts express
 * them as a deterministic conversation chain, often with locked controls or
 * `noReturnToGameCamAtEnd`, then let response and chosen scripts perform every
 * camera reset, control unlock, spawn, quest update, and zone transition.
 * Skipping therefore advances only through the retail chooseResponse path.
 * It never clears presentation flags and it stops at a real branch.
 */
#include "conversation_cutscene_skip.h"

#include "conversation_resume.h"
#include "conversation_resume_policy.h"
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
#define FN_CLOCK_RVA        EXE_RVA(0x0046dce0u)
#define FN_WAIT_OWNER_RVA   EXE_RVA(0x004d8770u)
#define FN_SCHEDULE_WAIT_RVA EXE_RVA(0x004d6a00u)
#define SCRIPT_CONTEXT_RVA  EXE_RVA(0x00787730u)

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
#define CLOCK_NOW_VSLOT     0x00160u
#define EXPRESSION_COUNT    0x0001cu
#define EXPRESSION_EVAL     0x0000cu

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

typedef struct ConversationTimedWaitScope {
    uint32_t owner_context;
    double claimed_at_s;
    int exhausted;
} ConversationTimedWaitScope;

static ConversationTimedWaitScope g_wait_scope;
static unsigned long g_wait_seen, g_wait_shortened, g_wait_unowned,
                     g_wait_foreign, g_wait_expired;

void fn_XMen2_004d9130(CPU *C);

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

    if (!cpu || !base || !self) return SLOT_NONE;
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

    call.esp -= 4u;
    WR32(call.esp, action);
    call.ecx = input;
    x86_guest_call_args(&call, RD32(vtable + INPUT_ACTION_MASK), 4u);
    return (uint8_t)call.eax;
}

int conversation_cutscene_skip_should_advance(CPU *cpu, uint32_t self,
                                               uint32_t slot, uint32_t input)
{
    ConversationCutsceneSnapshot state = snapshot(exe_base(), self, slot);
    int pressed = action20_down(cpu, input);
    if (pressed && state.readable && state.authored) {
        if (!g_policy.active) conversation_cutscene_skip_begin_sequence();
        x2_conversation_resume_manual_override();
    }
    ConversationSkipDecision decision = conversation_skip_policy_update(
        &g_policy, state.visible, state.readable && state.authored,
        state.controls_locked, pressed, state.response);
    return decision == CONVERSATION_SKIP_ADVANCE;
}

void conversation_cutscene_skip_observe_inactive(uint32_t self)
{
    ConversationCutsceneSnapshot state = snapshot(exe_base(), self, SLOT_NONE);

    /* An unreadable clock cannot prove cleanup restored control, so retain the
       latch until production state becomes readable again. */
    if (!state.readable) return;
    (void)conversation_skip_policy_update(
        &g_policy, 0, 0, state.controls_locked, 0,
        CONVERSATION_SKIP_RESPONSE_WAITING);
}

static void wait_scope_reset(void)
{
    memset(&g_wait_scope, 0, sizeof g_wait_scope);
}

void conversation_cutscene_skip_begin_sequence(void)
{
    wait_scope_reset();
}

static int wait_scope_timed_out(double now_s)
{
    if (!g_wait_scope.owner_context || g_wait_scope.exhausted) return 0;
    if (now_s - g_wait_scope.claimed_at_s <
        CONVERSATION_RESUME_WAIT_SECONDS)
        return 0;
    g_wait_scope.exhausted = 1;
    g_wait_expired++;
    return 1;
}

/* The owner is the exact scheduler context observed at SCRIPT_CONTEXT_RVA.
 * Global control locks are deliberately absent: they can outlive a
 * conversation and cannot identify which script owns a wait. */
static int sequence_fast_forwarding(uint32_t base)
{
    uint32_t context = 0;
    double now_s = guest_clock_elapsed_s();
    int sequence_active = g_policy.active ||
                          x2_conversation_resume_sequence_active();

    if (!sequence_active) {
        wait_scope_reset();
        return 0;
    }
    if (wait_scope_timed_out(now_s) || g_wait_scope.exhausted) return 0;
    if (!x86_peek32(base + SCRIPT_CONTEXT_RVA, &context) || !context)
        return 0;
    if (g_wait_scope.owner_context)
        return context == g_wait_scope.owner_context;
    if (!g_policy.active && !x2_conversation_resume_gate_open()) return 0;
    g_wait_scope.owner_context = context;
    g_wait_scope.claimed_at_s = now_s;
    return 1;
}

int conversation_cutscene_skip_owned_sequence_active(void)
{
    uint32_t base = exe_base();
    double now_s;

    if (!base || !g_wait_scope.owner_context) return 0;
    if (!g_policy.active && !x2_conversation_resume_sequence_active()) {
        wait_scope_reset();
        return 0;
    }
    if (g_wait_scope.exhausted) return 0;
    now_s = guest_clock_elapsed_s();
    if (wait_scope_timed_out(now_s)) return 0;
    return 1;
}

static int resolve_wait_expression(uint32_t parameter, uint32_t *expression,
                                   uint32_t *target)
{
    uint32_t count, vtable;

    if (!x86_peek32(parameter + EXPRESSION_COUNT, &count)) return 0;
    *expression = 0;
    if ((int32_t)count > 0 && !x86_peek32(parameter, expression)) return 0;
    if (!*expression || !x86_peek32(*expression, &vtable) ||
        !x86_peek32(vtable + EXPRESSION_EVAL, target) || !*target)
        return 0;
    return 1;
}

static void evaluate_wait_expression(CPU *C, uint32_t expression,
                                     uint32_t target)
{
    CPU call;
    call = *C;
    call.ecx = expression;
    x86_guest_call_args(&call, target, 0u);
}

static int schedule_immediate_wait(CPU *C, uint32_t base, uint32_t context)
{
    CPU call;
    uint32_t clock, vtable, now_target, now_bits;
    float now;

    call = *C;
    x86_guest_call(&call, base + FN_CLOCK_RVA);
    clock = call.eax;
    if (!clock || !x86_peek32(clock, &vtable) ||
        !x86_peek32(vtable + CLOCK_NOW_VSLOT, &now_target) || !now_target)
        return 0;

    call = *C;
    call.esp -= 4u;
    WR32(call.esp, context);
    call.ecx = clock;
    /* 0x00469740 is `FLD [ECX+0x3e8]; RET`: no arguments, no callee pop. */
    x86_guest_call_args(&call, now_target, 0u);
    now = (float)call.st[call.top];
    memcpy(&now_bits, &now, sizeof now_bits);

    call = *C;
    call.esp -= 8u;
    WR32(call.esp, now_bits);
    WR32(call.esp + 4u, context);
    x86_guest_call(&call, base + FN_WAIT_OWNER_RVA);
    call.ecx = call.eax;
    x86_guest_call_args(&call, base + FN_SCHEDULE_WAIT_RVA, 8u);
    return 1;
}

void x2_override_004d9130(CPU *C)
{
    uint32_t base = exe_base(), parameter, context = 0;
    uint32_t expression, expression_target;

    g_wait_seen++;
    if (!base || !x86_peek32(C->esp + 4u, &parameter) || !parameter ||
        !resolve_wait_expression(parameter, &expression,
                                 &expression_target) ||
        !x86_peek32(base + SCRIPT_CONTEXT_RVA, &context) ||
        !sequence_fast_forwarding(base) ||
        context != g_wait_scope.owner_context) {
        if (g_wait_scope.owner_context && context != g_wait_scope.owner_context)
            g_wait_foreign++;
        else
            g_wait_unowned++;
        fn_XMen2_004d9130(C);
        return;
    }
    evaluate_wait_expression(C, expression, expression_target);
    if (!schedule_immediate_wait(C, base, context)) {
        g_wait_unowned++;
        fn_XMen2_004d9130(C);
        return;
    }
    g_wait_shortened++;
    C->eax = 0u;
    C->esp += 4u;
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
{
    static const char *const response_name[] = {
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
    append(out, size, &at,
           "  authored skip: %s; %u request(s), %u retail response "
           "advance(s), %u blocked, %u ignored outside authored scenes\n",
           g_policy.active ? "ACTIVE" : "idle", g_policy.requests,
           g_policy.advances, g_policy.blocked, g_policy.ignored);
    append(out, size, &at,
           "  conversation waittimed: %lu/%lu shortened, %lu unowned, "
           "%lu foreign-context, %lu owner timeout(s); owner %s\n",
           g_wait_shortened, g_wait_seen, g_wait_unowned, g_wait_foreign,
           g_wait_expired,
           conversation_cutscene_skip_owned_sequence_active() ? "ACTIVE" :
                                                                "idle");
    return at;
}

__attribute__((constructor))
static void x2_conversation_cutscene_skip_register(void)
{
    x86_register_override("XMen2.exe", 0x004d9130u,
                          x2_override_004d9130);
}
