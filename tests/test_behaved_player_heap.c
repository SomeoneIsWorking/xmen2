#include "behaved_player.h"
#include "guest_memory.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

enum {
    ARENA_BASE = 0x31000000u,
    ARENA_SIZE = 0x00400000u,
    EXE_BASE = 0x00400000u,
    FN_SLOT_RELEASE = 0x004d5d00u,
    FN_POOL_RELEASE = 0x004d7c10u,
    FN_MANAGER = 0x004d8770u,
    FN_CONTEXT_CLEANUP = 0x004d8ea0u,
    FN_PUMP = 0x004d9640u,
    CONTEXT_POOL = 0x0002f2f4u,
    SCHEDULER_OFFSET = 0x0003a080u,
    CONTEXT_STRIDE = 0x5c4u,
    HEAP_OFFSET = 0x10cu,
    HEAP_COUNT = 0x200u,
    STACK = ARENA_BASE + ARENA_SIZE - 0x100u
};

typedef struct Pair {
    uint32_t deadline;
    uint32_t slot;
} Pair;

static uint32_t mapped_exe = EXE_BASE;
static X86Module module = {
    .name = "XMen2.exe",
    .base = &mapped_exe,
    .preferred = EXE_BASE
};
static struct {
    const char *module;
    uint32_t ep;
    x86_override_fn fn;
} registered;
static uint8_t runner_completed[30];
static char calls[64];
static unsigned call_count;
static unsigned failures;
static uint32_t manager_address = ARENA_BASE;

volatile uint32_t x2_write_watch_addr;

void x2_write_watch_fire(uint32_t address, uint32_t value)
{
    (void)address;
    (void)value;
    abort();
}

static void fail(const char *message)
{
    fprintf(stderr, "FAIL: %s\n", message);
    ++failures;
}

static void note(char event)
{
    if (call_count + 1u >= sizeof calls) abort();
    calls[call_count++] = event;
    calls[call_count] = '\0';
}

#define CHECK(condition, message) \
    do { if (!(condition)) fail(message); } while (0)

X86Module *x86_modules(void)
{
    return &module;
}

int x86_peek(uint32_t address, void *out, size_t size)
{
    uint64_t end = (uint64_t)address + size;
    if (address < ARENA_BASE || end > (uint64_t)ARENA_BASE + ARENA_SIZE)
        return 0;
    memcpy(out, guest_memory_const_pointer(address), size);
    return 1;
}

int x86_peek32(uint32_t address, uint32_t *out)
{
    return x86_peek(address, out, sizeof *out);
}

static unsigned fiber_of(uint32_t context)
{
    return (context - manager_address - CONTEXT_POOL) / CONTEXT_STRIDE;
}

uint32_t behaved_context_run(CPU *cpu, uint32_t context_address)
{
    unsigned fiber = fiber_of(context_address);

    (void)cpu;
    note('R');
    return runner_completed[fiber];
}

void x86_guest_call_args(CPU *cpu, uint32_t target,
                         uint32_t callee_pop_bytes)
{
    if (target == FN_MANAGER) {
        note('M');
        cpu->eax = manager_address;
    } else if (target == FN_CONTEXT_CLEANUP) {
        note('C');
    } else if (target == FN_POOL_RELEASE) {
        CHECK(cpu->ecx == manager_address + CONTEXT_POOL,
              "context release did not use the retail pool owner");
        CHECK(RD32(cpu->esp) < 30u,
              "context release received an invalid fiber index");
        note('F');
    } else if (target == FN_SLOT_RELEASE) {
        CHECK(cpu->ecx == manager_address + SCHEDULER_OFFSET,
              "scheduler release did not use the scheduler owner");
        CHECK(RD32(cpu->esp) < 30u,
              "scheduler release received an invalid slot");
        note('S');
    } else {
        fail("player called an unexpected guest function");
    }
    cpu->esp += callee_pop_bytes;
}

void x86_register_override(const char *name, uint32_t ep,
                           x86_override_fn function)
{
    registered.module = name;
    registered.ep = ep;
    registered.fn = function;
}

static uint32_t scheduler(void)
{
    return manager_address + SCHEDULER_OFFSET;
}

static uint32_t context(unsigned fiber)
{
    return manager_address + CONTEXT_POOL + fiber * CONTEXT_STRIDE;
}

static uint32_t float_bits(float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof bits);
    return bits;
}

static void set_heap(const float *deadlines, const uint32_t *slots,
                     unsigned count)
{
    unsigned index;
    uint32_t owner = scheduler();
    WR32(owner + HEAP_COUNT, count);
    for (index = 0; index < count; ++index) {
        uint32_t entry = owner + HEAP_OFFSET + index * 8u;
        WR32(entry, float_bits(deadlines[index]));
        WR32(entry + 4u, slots[index]);
        WR32(owner + slots[index] * 4u, context(slots[index]));
    }
}

static CPU fresh_cpu(void)
{
    CPU cpu;
    memset(&cpu, 0, sizeof cpu);
    cpu.esp = STACK;
    return cpu;
}

static int owns_mask(uint32_t candidate, void *opaque)
{
    uint32_t mask = *(const uint32_t *)opaque;
    return (mask & (1u << fiber_of(candidate))) != 0u;
}

static int pair_equal(Pair left, Pair right)
{
    return left.deadline == right.deadline && left.slot == right.slot;
}

static void check_unowned_pairs(const Pair *before, unsigned before_count,
                                Pair selected)
{
    Pair after[30];
    uint8_t used[30] = {0};
    unsigned index, other, after_count = RD32(scheduler() + HEAP_COUNT);
    CHECK(after_count + 1u == before_count,
          "an owned step removed the wrong number of heap entries");
    for (index = 0; index < after_count; ++index)
        x86_peek(scheduler() + HEAP_OFFSET + index * 8u,
                 &after[index], sizeof after[index]);
    for (index = 0; index < before_count; ++index) {
        int found = 0;
        if (pair_equal(before[index], selected)) continue;
        for (other = 0; other < after_count; ++other)
            if (!used[other] && pair_equal(before[index], after[other])) {
                used[other] = 1u;
                found = 1;
                break;
            }
        CHECK(found, "an unowned deadline/slot pair changed during removal");
    }
}

static void test_read_only_selection(void)
{
    static const float deadlines[] = {1.0f, 4.0f, 2.0f, 8.0f, 5.0f};
    static const uint32_t slots[] = {0u, 1u, 2u, 3u, 4u};
    uint8_t before[0x204];
    uint32_t mask = (1u << 1) | (1u << 4);
    uint32_t selected = 0;
    CPU cpu = fresh_cpu();

    set_heap(deadlines, slots, 5u);
    memcpy(before, guest_memory_const_pointer(scheduler()), sizeof before);
    CHECK(behaved_player_next_owned(&cpu, owns_mask, &mask, &selected) == 1,
          "read-only selection refused a valid heap");
    CHECK(selected == context(1u),
          "read-only selection did not choose the earliest owned context");
    CHECK(!memcmp(before, guest_memory_const_pointer(scheduler()), sizeof before),
          "read-only selection changed scheduler bytes");
}

static void test_arbitrary_remove_bubbles_up(void)
{
    static const float deadlines[] = {1.0f, 10.0f, 2.0f, 11.0f,
                                      12.0f, 3.0f, 4.0f};
    static const uint32_t slots[] = {0u, 1u, 2u, 3u, 4u, 5u, 6u};
    Pair before[7], selected;
    unsigned index;
    CPU cpu = fresh_cpu();

    set_heap(deadlines, slots, 7u);
    for (index = 0; index < 7u; ++index)
        x86_peek(scheduler() + HEAP_OFFSET + index * 8u,
                 &before[index], sizeof before[index]);
    selected = before[4];
    calls[0] = '\0'; call_count = 0;
    CHECK(behaved_player_step_context(&cpu, context(4u)) ==
              BEHAVED_PLAYER_STEP_RAN,
          "exact-context step did not run the selected context");
    CHECK(!strcmp(calls, "MRS"),
          "yielding context did not follow manager/run/slot-release order");
    CHECK(RD32(scheduler() + HEAP_OFFSET + 8u) == float_bits(4.0f),
          "arbitrary removal did not bubble a replacement upward");
    check_unowned_pairs(before, 7u, selected);
}

static void test_arbitrary_remove_bubbles_down(void)
{
    static const float deadlines[] = {1.0f, 2.0f, 3.0f, 4.0f,
                                      5.0f, 6.0f, 7.0f};
    static const uint32_t slots[] = {0u, 1u, 2u, 3u, 4u, 5u, 6u};
    Pair before[7], selected;
    unsigned index;
    CPU cpu = fresh_cpu();

    set_heap(deadlines, slots, 7u);
    for (index = 0; index < 7u; ++index)
        x86_peek(scheduler() + HEAP_OFFSET + index * 8u,
                 &before[index], sizeof before[index]);
    selected = before[1];
    CHECK(behaved_player_step_context(&cpu, context(1u)) ==
              BEHAVED_PLAYER_STEP_RAN,
          "downward arbitrary removal did not run");
    CHECK(RD32(scheduler() + HEAP_OFFSET + 8u) == float_bits(4.0f),
          "arbitrary removal did not bubble a replacement downward");
    check_unowned_pairs(before, 7u, selected);
}

static void test_completion_order(void)
{
    static const float deadlines[] = {9.0f};
    static const uint32_t slots[] = {7u};
    CPU cpu = fresh_cpu();

    set_heap(deadlines, slots, 1u);
    runner_completed[7] = 1u;
    calls[0] = '\0'; call_count = 0;
    CHECK(behaved_player_step_context(&cpu, context(7u)) ==
              BEHAVED_PLAYER_STEP_COMPLETED,
          "completed context was not reported complete");
    CHECK(!strcmp(calls, "MRCMFS"),
          "completion did not run cleanup/pool/slot release in retail order");
    runner_completed[7] = 0u;
}

static void test_strict_ordinary_deadline(void)
{
    static const float deadlines[] = {5.0f, 10.0f};
    static const uint32_t slots[] = {0u, 1u};
    CPU cpu = fresh_cpu();

    set_heap(deadlines, slots, 2u);
    cpu.ecx = scheduler();
    WR32(cpu.esp, 0xabcdef01u);
    WR32(cpu.esp + 4u, float_bits(5.0f));
    calls[0] = '\0'; call_count = 0;
    registered.fn(&cpu);
    CHECK(RD32(scheduler() + HEAP_COUNT) == 2u,
          "ordinary pump treated deadline == now as due");
    CHECK(call_count == 0u, "ordinary equality invoked a guest context");
    CHECK(cpu.esp == STACK + 8u, "ordinary override did not reproduce RET 4");

    cpu = fresh_cpu();
    cpu.ecx = scheduler();
    WR32(cpu.esp, 0xabcdef02u);
    WR32(cpu.esp + 4u, float_bits(6.0f));
    calls[0] = '\0'; call_count = 0;
    registered.fn(&cpu);
    CHECK(RD32(scheduler() + HEAP_COUNT) == 1u,
          "ordinary pump did not consume the strictly due root");
    CHECK(!strcmp(calls, "RS"),
          "ordinary pump did not use the shared run/release path");
    CHECK(RD32(scheduler() + HEAP_OFFSET) == float_bits(10.0f),
          "ordinary root removal did not preserve the next deadline");
}

static void test_corruption_refuses_without_mutation(void)
{
    static const float deadlines[] = {1.0f};
    static const uint32_t slots[] = {0u};
    uint8_t before[0x204];
    uint32_t mask = 1u, selected;
    CPU cpu = fresh_cpu();

    set_heap(deadlines, slots, 1u);
    WR32(scheduler() + HEAP_COUNT, 31u);
    memcpy(before, guest_memory_const_pointer(scheduler()), sizeof before);
    CHECK(behaved_player_next_owned(&cpu, owns_mask, &mask, &selected) == -1,
          "oversized heap count was not refused");
    CHECK(!memcmp(before, guest_memory_const_pointer(scheduler()), sizeof before),
          "count refusal changed scheduler bytes");

    set_heap(deadlines, slots, 1u);
    WR32(scheduler() + HEAP_OFFSET + 4u, 30u);
    CHECK(behaved_player_next_owned(&cpu, owns_mask, &mask, &selected) == -1,
          "out-of-range scheduler slot was not refused");

    set_heap(deadlines, slots, 1u);
    WR32(scheduler(), context(0u) + 1u);
    CHECK(behaved_player_next_owned(&cpu, owns_mask, &mask, &selected) == -1,
          "misaligned context pointer was not refused");
}

int main(void)
{
    if (guest_memory_init() != 0 ||
        guest_memory_map_fixed(ARENA_BASE, ARENA_SIZE,
                               PROT_READ | PROT_WRITE) != 0) {
        fprintf(stderr, "FAIL: could not map isolated guest arena\n");
        return 1;
    }
    CHECK(registered.module && !strcmp(registered.module, "XMen2.exe") &&
              registered.ep == FN_PUMP && registered.fn,
          "FUN_004d9640 override did not self-register");
    test_read_only_selection();
    test_arbitrary_remove_bubbles_up();
    test_arbitrary_remove_bubbles_down();
    test_completion_order();
    test_strict_ordinary_deadline();
    test_corruption_refuses_without_mutation();
    return failures != 0u;
}
