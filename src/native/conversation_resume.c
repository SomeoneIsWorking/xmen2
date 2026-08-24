#include "conversation_resume.h"

#include "conversation_cutscene_skip.h"
#include "conversation_resume_policy.h"
#include "guest_clock.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include <stdio.h>

#define CVF_VISIBLE 0x02u
#define CVF_ENDING  0x08u
#define CV_APPLY_TIMER 0x21b5cu
#define EXE_PREFERRED 0x00400000u
#define FN_CLOCK_RVA  0x0006dce0u
#define CLOCK_NOW_VSLOT 0x160u

static X2ConversationResumePolicy g_policy;
static int g_pending_continue;
static unsigned g_continue_started;
static unsigned g_pending_cancelled;
static unsigned g_map_returns;
static unsigned g_map_failures;

static uint32_t exe_base(void)
{
    X86Module *module;
    for (module = x86_modules(); module; module = module->next)
        if (module->preferred == EXE_PREFERRED && *module->base)
            return *module->base;
    return 0;
}

void x2_conversation_resume_continue_started(void)
{
    g_pending_continue = 1;
    g_continue_started++;
}

void x2_conversation_resume_cancel_pending(void)
{
    if (!g_pending_continue) return;
    g_pending_continue = 0;
    g_pending_cancelled++;
}

void x2_conversation_resume_map_return(int succeeded)
{
    if (!g_pending_continue) return;
    g_map_returns++;
    g_pending_continue = 0;
    if (!succeeded) {
        g_map_failures++;
        return;
    }
    conversation_cutscene_skip_begin_sequence();
    x2_conversation_resume_policy_arm(&g_policy, guest_clock_elapsed_s());
}

void x2_conversation_resume_observe(struct CPU *cpu, uint32_t self,
                                    uint8_t flags)
{
    ConversationSkipResponse response =
        conversation_cutscene_skip_response(cpu, self);
    x2_conversation_resume_policy_observe(
        &g_policy, !!(flags & CVF_VISIBLE), !!(flags & CVF_ENDING),
        conversation_cutscene_skip_owned_sequence_active(), response,
        guest_clock_elapsed_s());
}

int x2_conversation_resume_gate_open(void)
{
    (void)x2_conversation_resume_policy_expire(
        &g_policy, guest_clock_elapsed_s());
    return x2_conversation_resume_policy_should_advance(&g_policy);
}

int x2_conversation_resume_should_advance(CPU *cpu, uint32_t self)
{
    CPU call;
    uint32_t base, clock, vtable, target;
    long double now;

    if (!x2_conversation_resume_gate_open() || !cpu || !self ||
        !(base = exe_base()))
        return 0;
    call = *cpu;
    x86_guest_call(&call, base + FN_CLOCK_RVA);
    clock = call.eax;
    if (!clock) return 0;
    vtable = RD32(clock);
    target = RD32(vtable + CLOCK_NOW_VSLOT);
    call = *cpu;
    call.ecx = clock;
    x86_guest_call_args(&call, target, 0u);
    now = call.st[call.top];
    if (now <= (long double)RDF32(self + CV_APPLY_TIMER)) return 0;
    x2_conversation_resume_policy_note_advance(&g_policy);
    return 1;
}

int x2_conversation_resume_sequence_active(void)
{
    (void)x2_conversation_resume_policy_expire(
        &g_policy, guest_clock_elapsed_s());
    return x2_conversation_resume_policy_active(&g_policy);
}

void x2_conversation_resume_manual_override(void)
{
    x2_conversation_resume_policy_manual_override(&g_policy);
}

void x2_conversation_resume_report(void)
{
    printf("        Continue resume: %s; %u accepted, %u map return(s), "
           "%u map failure(s), %u pending cancellation(s); %u armed, "
           "%u observation(s), %u retail advance(s), %u choice handoff(s), "
           "%u retired, %u expired, %u manual override(s)\n",
           g_pending_continue ? "PENDING" :
           (x2_conversation_resume_policy_active(&g_policy) ? "ACTIVE" :
                                                             "idle"),
           g_continue_started, g_map_returns, g_map_failures,
           g_pending_cancelled, g_policy.armed, g_policy.observations,
           g_policy.advances, g_policy.handed_back, g_policy.retired,
           g_policy.expired, g_policy.manual_overrides);
}
