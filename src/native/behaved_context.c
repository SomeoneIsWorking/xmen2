/* BehavEd command-graph interpreter, ported from XMen2.exe 004d8b30. */
#include "behaved_context.h"

#include "x86rt.h"
#include "x86rt_native.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

enum {
    EXE_PREFERRED = 0x00400000u,
    FN_TREE_FIND = 0x00456440u,
    FN_VALUE_FIND = 0x004d59e0u,
    FN_VALUE_RELEASE = 0x004d5ff0u,
    FN_MANAGER = 0x004d8770u,
    FN_CONTEXT_RUN = 0x004d8b30u,
    CURRENT_CONTEXT_RVA = 0x00387730u,
    INVALID_INDEX = 0x3fffffffu,
    VALUE_POOL = 0x5db8u,
    VALUE_POOL_COUNT = 0x614u,
    VALUE_ALLOCATED_BITS = 0xbfccu,
    VALUE_STRIDE = 0x0cu,
    ARGUMENT_CAPACITY = 7u
};

static uint32_t exe_base(void)
{
    const X86Module *module;

    for (module = x86_modules(); module; module = module->next)
        if (module->preferred == EXE_PREFERRED && module->base &&
            *module->base)
            return *module->base;
    return 0;
}

static uint32_t linked(uint32_t base, uint32_t preferred)
{
    return base + preferred - EXE_PREFERRED;
}

static uint32_t guest_call(const CPU *source, uint32_t target,
                           uint32_t self, const uint32_t *arguments,
                           size_t count, uint32_t callee_pop_bytes)
{
    CPU call = *source;

    while (count) {
        call.esp -= 4u;
        WR32(call.esp, arguments[--count]);
    }
    call.ecx = self;
    x86_guest_call_args(&call, target, callee_pop_bytes);
    return call.eax;
}

static uint32_t find_value(const CPU *cpu, uint32_t base,
                           uint32_t owner, uint32_t key, uint32_t root)
{
    CPU frame = *cpu;
    uint32_t pair = cpu->esp - 0x20u;
    const uint32_t arguments[] = {pair, root};

    WR32(pair, key);
    frame.esp = cpu->esp - 0x60u;
    return guest_call(&frame, linked(base, FN_VALUE_FIND), owner,
                      arguments, 2u, 8u);
}

static uint32_t resolve_argument(const CPU *cpu, uint32_t base,
                                 uint32_t context, uint32_t raw)
{
    uint32_t root = RD32(context + 0x0cu);
    uint32_t index = root;

    if (root != INVALID_INDEX) {
        uint32_t branch = context + 8u + root * 16u;
        uint32_t split = RD32(branch + 0x18u);

        if (raw < split)
            index = find_value(cpu, base, context + 8u, raw,
                               RD32(branch + 0x10u));
        else if (raw > split)
            index = find_value(cpu, base, context + 8u, raw,
                               RD32(branch + 0x14u));
    }
    if (index != INVALID_INDEX) {
        uint32_t replacement = RD32(context + 0x170u + index * 4u);
        if (replacement) return replacement;
    }
    return raw;
}

static uint32_t call_handler(CPU *cpu, uint32_t base, uint32_t context,
                             uint32_t node)
{
    CPU frame = *cpu;
    uint32_t list = cpu->esp - 0x40u;
    uint32_t count = RD32(node + 0x24u);
    uint32_t arguments[] = {list};
    uint32_t index;

    if (count > ARGUMENT_CAPACITY) {
        fprintf(stderr, "BEHAVED: node 0x%08x has %u arguments; retail "
                "004d8b30 provides storage for at most %u\n",
                node, count, ARGUMENT_CAPACITY);
        x86_diag_dump();
        abort();
    }
    for (index = 0; index < count; ++index)
        WR32(list + index * 4u,
             resolve_argument(cpu, base, context,
                              RD32(node + 8u + index * 4u)));
    WR32(list + 0x1cu, count);
    frame.esp = cpu->esp - 0x80u;
    return guest_call(&frame, RD32(node + 0x28u), 0u,
                      arguments, 1u, 0u);
}

static uint32_t find_script_value(const CPU *cpu, uint32_t base,
                                  uint32_t script, uint32_t node)
{
    CPU frame = *cpu;
    uint32_t pair = cpu->esp - 0x28u;
    const uint32_t arguments[] = {pair, RD32(script + 8u)};

    WR32(pair, RD32(node));
    WR32(pair + 4u, RD32(node + 4u));
    frame.esp = cpu->esp - 0x68u;
    return guest_call(&frame, linked(base, FN_TREE_FIND), script + 4u,
                      arguments, 2u, 8u);
}

static void assign_result(CPU *cpu, uint32_t base, uint32_t context,
                          uint32_t node, uint32_t result)
{
    uint32_t name = RD32(node);
    uint32_t script, script_index, key, context_index, destination;
    uint32_t arguments[] = {result};

    if (!name || RD8(name) == 0u) return;
    script = RD32(context + 4u);
    script_index = find_script_value(cpu, base, script, node);
    key = script_index == INVALID_INDEX
        ? 0u : RD32(script + 0x1acu + script_index * 8u);
    context_index = find_value(cpu, base, context + 8u, key,
                               RD32(context + 0x0cu));
    if (context_index == INVALID_INDEX) return;
    destination = RD32(context + 0x170u + context_index * 4u);
    if (!destination) return;
    guest_call(cpu, RD32(RD32(destination) + 0x18u), destination,
               arguments, 1u, 4u);
}

static int result_is_true(CPU *cpu, uint32_t result)
{
    return guest_call(cpu, RD32(RD32(result) + 0x10u), result,
                      NULL, 0u, 0u) == 1u;
}

static void release_result(CPU *cpu, uint32_t base, uint32_t result)
{
    uint32_t manager, pool, distance, index, arguments[1];

    if (!result) return;
    manager = guest_call(cpu, linked(base, FN_MANAGER), 0u,
                         NULL, 0u, 0u);
    pool = manager + VALUE_POOL;
    if (result < pool) return;
    distance = result - pool;
    if (distance % VALUE_STRIDE != 0u) return;
    index = distance / VALUE_STRIDE;
    if (index >= VALUE_POOL_COUNT ||
        !(RD32(manager + VALUE_ALLOCATED_BITS + (index / 32u) * 4u) &
          (1u << (index % 32u))))
        return;
    arguments[0] = index;
    guest_call(cpu, linked(base, FN_VALUE_RELEASE), pool,
               arguments, 1u, 4u);
}

uint32_t behaved_context_run(CPU *cpu, uint32_t context)
{
    uint32_t base = exe_base();
    uint32_t current_address, prior, script, node, pending;

    if (!cpu || !base || !context) return 0u;
    current_address = base + CURRENT_CONTEXT_RVA;
    prior = RD32(current_address);
    WR32(current_address, context);
    script = RD32(context + 4u);
    node = script ? RD32(script) : 0u;
    pending = RD32(context);
    if (pending) {
        WR32(context, 0u);
        node = pending;
    }
    while (node) {
        uint32_t handler = RD32(node + 0x28u);
        uint32_t result;
        uint32_t next;

        if (!handler) {
            node = RD32(node + 0x2cu);
            continue;
        }
        result = call_handler(cpu, base, context, node);
        assign_result(cpu, base, context, node, result);
        next = RD32(node + 0x2cu);
        if (RD8(node + 0x34u) == 1u) {
            if (next) WR32(context, next);
            next = 0u;
        } else if (RD8(node + 0x35u) == 1u &&
                   !result_is_true(cpu, result)) {
            next = RD32(node + 0x30u);
        }
        release_result(cpu, base, result);
        node = next;
    }
    pending = RD32(context);
    WR32(current_address, prior);
    return (pending & 0xffffff00u) | (pending == 0u);
}

void x2_override_004d8b30(CPU *cpu)
{
    if (!cpu) return;
    cpu->eax = behaved_context_run(cpu, cpu->ecx);
    cpu->esp += 4u;
}

__attribute__((constructor))
static void x2_behaved_context_register_override(void)
{
    x86_register_override("XMen2.exe", FN_CONTEXT_RUN,
                          x2_override_004d8b30);
}
