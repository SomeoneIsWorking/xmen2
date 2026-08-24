#define _GNU_SOURCE

#include "conversation_cutscene_skip.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

#define REGION_BYTES       0x00400000u
#define EXE_PREFERRED      0x00400000u
#define SCRIPT_CONTEXT_RVA 0x00387730u
#define FN_CLOCK_RVA       0x0006dce0u
#define FN_WAIT_OWNER_RVA  0x000d8770u
#define FN_SCHEDULE_RVA    0x000d6a00u
#define EXPRESSION_TARGET  0x00012000u
#define CLOCK_TARGET       0x00013000u

static uint32_t g_base;
static X86Module g_module;
static double g_elapsed;
static int g_resume_active;
static int g_resume_gate;
static int g_original_calls;
static int g_expression_calls;
static int g_clock_pop = -1;
static int g_schedule_pop = -1;
static int g_schedule_calls;
static uint32_t g_scheduled_context;
static float g_scheduled_deadline;
static uint32_t g_clock_object;
static uint32_t g_wait_owner;
static int failures;

int native_stubs_registered(const char *module, uint32_t linked_ep);

#define CHECK(expr) check(!!(expr), #expr, __LINE__)

static void check(int passed, const char *expression, int line)
{
    if (passed) return;
    fprintf(stderr, "line %d: CHECK(%s) failed\n", line, expression);
    failures++;
}

int x86_peek32(uint32_t address, uint32_t *out)
{
    if (address < g_base || address > g_base + REGION_BYTES - 4u) return 0;
    memcpy(out, (const void *)(uintptr_t)address, sizeof *out);
    return 1;
}

X86Module *x86_modules(void)
{
    return &g_module;
}

double guest_clock_elapsed_s(void)
{
    return g_elapsed;
}

int x2_conversation_resume_sequence_active(void)
{
    return g_resume_active;
}

int x2_conversation_resume_gate_open(void)
{
    return g_resume_gate;
}

void x2_conversation_resume_manual_override(void)
{
}

void x86_guest_call(CPU *cpu, uint32_t target)
{
    if (target == g_base + FN_CLOCK_RVA) {
        cpu->eax = g_clock_object;
        return;
    }
    if (target == g_base + FN_WAIT_OWNER_RVA) {
        cpu->eax = g_wait_owner;
        return;
    }
    fprintf(stderr, "unexpected x86_guest_call target 0x%08x\n", target);
    failures++;
}

void x86_guest_call_args(CPU *cpu, uint32_t target,
                         uint32_t callee_pop_bytes)
{
    if (target == g_base + EXPRESSION_TARGET) {
        CHECK(callee_pop_bytes == 0u);
        g_expression_calls++;
        cpu->st[cpu->top] = 123.0L;
        return;
    }
    if (target == g_base + CLOCK_TARGET) {
        g_clock_pop = (int)callee_pop_bytes;
        cpu->st[cpu->top] = 42.25L;
        return;
    }
    if (target == g_base + FN_SCHEDULE_RVA) {
        uint32_t bits;
        g_schedule_pop = (int)callee_pop_bytes;
        g_schedule_calls++;
        memcpy(&bits, (const void *)(uintptr_t)cpu->esp, sizeof bits);
        memcpy(&g_scheduled_deadline, &bits, sizeof bits);
        memcpy(&g_scheduled_context,
               (const void *)(uintptr_t)(cpu->esp + 4u),
               sizeof g_scheduled_context);
        return;
    }
    fprintf(stderr, "unexpected x86_guest_call_args target 0x%08x\n",
            target);
    failures++;
}

void fn_XMen2_004d9130(CPU *cpu)
{
    g_original_calls++;
    cpu->esp += 4u;
}

static CPU make_call(uint32_t stack, uint32_t parameter)
{
    CPU cpu;
    memset(&cpu, 0, sizeof cpu);
    cpu.esp = stack;
    cpu.top = 0;
    WR32(stack, 0xcafebabeu);
    WR32(stack + 4u, parameter);
    return cpu;
}

static void run_wait(uint32_t context, double elapsed, CPU *cpu)
{
    WR32(g_base + SCRIPT_CONTEXT_RVA, context);
    g_elapsed = elapsed;
    x2_override_004d9130(cpu);
}

int main(void)
{
    uint32_t stack, parameter, malformed, expression, expression_vtable;
    uint32_t clock_vtable;
    uint32_t context_a, context_b;
    CPU cpu;

    void *region = mmap(NULL, REGION_BYTES, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    if (region == MAP_FAILED || (uintptr_t)region > UINT32_MAX) {
        perror("mmap(MAP_32BIT)");
        return 1;
    }
    g_base = (uint32_t)(uintptr_t)region;
    memset(&g_module, 0, sizeof g_module);
    g_module.name = "XMen2.exe";
    g_module.base = &g_base;
    g_module.preferred = EXE_PREFERRED;

    stack = g_base + 0x1000u;
    parameter = g_base + 0x2000u;
    malformed = g_base + 0xa000u;
    expression = g_base + 0x3000u;
    expression_vtable = g_base + 0x4000u;
    g_clock_object = g_base + 0x5000u;
    clock_vtable = g_base + 0x6000u;
    g_wait_owner = g_base + 0x7000u;
    context_a = g_base + 0x8000u;
    context_b = g_base + 0x9000u;

    WR32(parameter, expression);
    WR32(parameter + 0x1cu, 1u);
    WR32(expression, expression_vtable);
    WR32(expression_vtable + 0x0cu, g_base + EXPRESSION_TARGET);
    WR32(g_clock_object, clock_vtable);
    WR32(clock_vtable + 0x160u, g_base + CLOCK_TARGET);

    g_resume_active = 1;
    g_resume_gate = 1;
    cpu = make_call(stack, malformed);
    run_wait(context_a, 0.5, &cpu);
    CHECK(g_original_calls == 1);
    CHECK(!conversation_cutscene_skip_owned_sequence_active());

    cpu = make_call(stack, parameter);
    run_wait(context_a, 1.0, &cpu);
    CHECK(g_original_calls == 1);
    CHECK(g_expression_calls == 1);
    CHECK(g_clock_pop == 0);
    CHECK(g_schedule_pop == 8);
    CHECK(g_schedule_calls == 1);
    CHECK(g_scheduled_context == context_a);
    CHECK(g_scheduled_deadline == 42.25f);
    CHECK(cpu.eax == 0u);
    CHECK(cpu.esp == stack + 4u);

    cpu = make_call(stack, parameter);
    run_wait(context_b, 2.0, &cpu);
    CHECK(g_original_calls == 2);
    CHECK(g_schedule_calls == 1);

    cpu = make_call(stack, parameter);
    run_wait(context_a, 3.0, &cpu);
    CHECK(g_schedule_calls == 2);

    g_resume_active = 0;
    g_resume_gate = 0;
    cpu = make_call(stack, parameter);
    run_wait(context_a, 4.0, &cpu);
    CHECK(g_original_calls == 3);

    /* An active WAITING policy cannot claim a context. This is the negative
       case that an unrelated global control lock used to turn into a skip. */
    g_resume_active = 1;
    cpu = make_call(stack, parameter);
    run_wait(context_b, 5.0, &cpu);
    CHECK(g_original_calls == 4);
    CHECK(g_schedule_calls == 2);

    g_resume_gate = 1;
    cpu = make_call(stack, parameter);
    run_wait(context_b, 10.0, &cpu);
    CHECK(g_schedule_calls == 3);
    CHECK(conversation_cutscene_skip_owned_sequence_active());

    cpu = make_call(stack, parameter);
    run_wait(context_b, 20.0, &cpu);
    CHECK(g_original_calls == 5);
    CHECK(g_schedule_calls == 3);
    CHECK(!conversation_cutscene_skip_owned_sequence_active());

    /* A bounded-out owner belongs only to the sequence that claimed it. A
       later Continue/manual sequence must be able to claim a new context. */
    conversation_cutscene_skip_begin_sequence();
    cpu = make_call(stack, parameter);
    run_wait(context_a, 21.0, &cpu);
    CHECK(g_original_calls == 5);
    CHECK(g_schedule_calls == 4);
    CHECK(g_scheduled_context == context_a);

    CHECK(native_stubs_registered("XMen2.exe", 0x004d9130u));
    if (failures)
        fprintf(stderr, "%d conversation waittimed check(s) failed\n",
                failures);
    return failures != 0;
}
