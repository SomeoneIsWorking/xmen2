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
#include "conversation_skip_policy.h"
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
    if (pressed && state.readable && state.authored) {
        x2_conversation_resume_manual_override();
    }
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
        x2_conversation_resume_manual_override();
    }
    (void)conversation_skip_policy_update(
        &g_policy, 0, 0, state.controls_locked, 0,
        CONVERSATION_SKIP_RESPONSE_WAITING);
}

size_t conversation_cutscene_skip_status(char *out, size_t size)
{
    int wrote;
    if (!out || !size) return 0;
    wrote = snprintf(
        out, size,
        "authored skip %s: %lu action check(s), %lu DOWN; %u request(s), "
        "%u retail response advance(s), %u blocked, %u ignored, %u "
        "completed after control unlock; retail waittimed untouched (no "
        "native override)",
        g_policy.active ? "ACTIVE" : "idle", g_action_checks,
        g_action_down, g_policy.requests, g_policy.advances,
        g_policy.blocked, g_policy.ignored, g_policy.completed);
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
    append(out, size, &at, "  ");
    at += conversation_cutscene_skip_status(out + at, size - at);
    append(out, size, &at, "\n");
    return at;
}
