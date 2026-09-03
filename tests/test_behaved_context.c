#include "behaved_context.h"
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
  ARENA_SIZE = 0x00500000u,
  FN_TREE_FIND = ARENA_BASE + 0x00056440u,
  FN_VALUE_FIND = ARENA_BASE + 0x000d59e0u,
  FN_VALUE_RELEASE = ARENA_BASE + 0x000d5ff0u,
  FN_MANAGER = ARENA_BASE + 0x000d8770u,
  FN_CONTEXT_RUN = 0x004d8b30u,
  CURRENT_CONTEXT = ARENA_BASE + 0x00387730u,
  HANDLER_TRUE = ARENA_BASE + 0x00210000u,
  HANDLER_FALSE = ARENA_BASE + 0x00210010u,
  HANDLER_FORBIDDEN = ARENA_BASE + 0x00210020u,
  METHOD_BOOL_TRUE = ARENA_BASE + 0x00210100u,
  METHOD_BOOL_FALSE = ARENA_BASE + 0x00210110u,
  METHOD_ASSIGN = ARENA_BASE + 0x00210120u,
  SCRIPT = ARENA_BASE + 0x00300000u,
  CONTEXT = ARENA_BASE + 0x00310000u,
  NODE_1 = ARENA_BASE + 0x00320000u,
  NODE_2 = ARENA_BASE + 0x00320100u,
  NODE_3 = ARENA_BASE + 0x00320200u,
  NODE_4 = ARENA_BASE + 0x00320300u,
  NODE_5 = ARENA_BASE + 0x00320400u,
  NAME = ARENA_BASE + 0x00330000u,
  DESTINATION = ARENA_BASE + 0x00330100u,
  DESTINATION_VTABLE = ARENA_BASE + 0x00330200u,
  TRUE_VTABLE = ARENA_BASE + 0x00330300u,
  FALSE_VTABLE = ARENA_BASE + 0x00330400u,
  MANAGER = ARENA_BASE + 0x00340000u,
  VALUE_POOL = 0x5db8u,
  VALUE_ALLOCATED_BITS = 0xbfccu,
  VALUE_TRUE_INDEX = 3u,
  VALUE_FALSE_INDEX = 4u,
  STACK = ARENA_BASE + ARENA_SIZE - 0x1000u,
  INVALID_INDEX = 0x3fffffffu
};

static uint32_t mapped_exe = ARENA_BASE;
static X86Module module = {
    .name = "XMen2.exe",
    .base = &mapped_exe,
    .preferred = 0x00400000u,
};
static x86_override_fn registered;
static unsigned failures;
static unsigned handler_true_calls;
static unsigned handler_false_calls;
static unsigned assignments;
static unsigned releases;

volatile uint32_t x2_write_watch_addr;

void x2_write_watch_fire(uint32_t address, uint32_t value) {
  (void)address;
  (void)value;
  abort();
}

static void check(int condition, const char *message) {
  if (condition)
    return;
  fprintf(stderr, "FAIL: %s\n", message);
  failures++;
}

X86Module *x86_modules(void) { return &module; }

int x86_peek(uint32_t address, void *out, size_t size) {
  uint64_t end = (uint64_t)address + size;

  if (address < ARENA_BASE || end > (uint64_t)ARENA_BASE + ARENA_SIZE)
    return 0;
  memcpy(out, guest_memory_const_pointer(address), size);
  return 1;
}

int x86_peek32(uint32_t address, uint32_t *out) {
  return x86_peek(address, out, sizeof *out);
}

void x86_diag_dump(void) {}

static uint32_t true_result(void) {
  return MANAGER + VALUE_POOL + VALUE_TRUE_INDEX * 12u;
}

static uint32_t false_result(void) {
  return MANAGER + VALUE_POOL + VALUE_FALSE_INDEX * 12u;
}

static void check_arguments(CPU *cpu, uint32_t expected_count, uint32_t first) {
  uint32_t list = RD32(cpu->esp);

  check(RD32(list + 0x1cu) == expected_count,
        "handler received the wrong BehavEd argument count");
  if (expected_count)
    check(RD32(list) == first, "handler received the wrong first argument");
}

static void clobber_callee_frame(CPU *cpu) {
  memset(guest_memory_pointer(cpu->esp - 0x40u), 0xcc, 0x40u);
}

void x86_guest_call_args(CPU *cpu, uint32_t target, uint32_t callee_pop_bytes) {
  if (target == FN_MANAGER) {
    cpu->eax = MANAGER;
  } else if (target == FN_TREE_FIND) {
    check(cpu->ecx == SCRIPT + 4u,
          "return-name lookup used the wrong script map");
    check(RD32(RD32(cpu->esp)) == NAME,
          "return-name lookup lost the authored name");
    cpu->eax = 2u;
  } else if (target == FN_VALUE_FIND) {
    check(cpu->ecx == CONTEXT + 8u, "context value lookup used the wrong map");
    cpu->eax = RD32(RD32(cpu->esp)) == 0x33u ? 1u : INVALID_INDEX;
  } else if (target == FN_VALUE_RELEASE) {
    check(cpu->ecx == MANAGER + VALUE_POOL,
          "result release used the wrong pool owner");
    check(RD32(cpu->esp) == VALUE_TRUE_INDEX ||
              RD32(cpu->esp) == VALUE_FALSE_INDEX,
          "result release used the wrong value index");
    releases++;
  } else if (target == HANDLER_TRUE) {
    uint32_t list = RD32(cpu->esp);
    clobber_callee_frame(cpu);
    check_arguments(cpu, RD32(list + 0x1cu), RD32(list));
    handler_true_calls++;
    cpu->eax = true_result();
  } else if (target == HANDLER_FALSE) {
    clobber_callee_frame(cpu);
    check_arguments(cpu, 0u, 0u);
    handler_false_calls++;
    cpu->eax = false_result();
  } else if (target == HANDLER_FORBIDDEN) {
    check(0, "conditional edge executed the non-authored branch");
    cpu->eax = true_result();
  } else if (target == METHOD_BOOL_TRUE) {
    cpu->eax = 1u;
  } else if (target == METHOD_BOOL_FALSE) {
    cpu->eax = 0u;
  } else if (target == METHOD_ASSIGN) {
    check(cpu->ecx == DESTINATION,
          "result assignment used the wrong destination");
    check(RD32(cpu->esp) == true_result(),
          "result assignment lost the handler result");
    assignments++;
  } else {
    check(0, "native interpreter called an unexpected guest target");
  }
  cpu->esp += callee_pop_bytes;
}

void x86_register_override(const char *name, uint32_t entry,
                           x86_override_fn function) {
  check(!strcmp(name, "XMen2.exe"),
        "context override registered against the wrong module");
  check(entry == FN_CONTEXT_RUN,
        "context override registered at the wrong entry point");
  registered = function;
}

static void set_node(uint32_t node, uint32_t handler, uint32_t next,
                     uint32_t alternate, uint8_t suspend, uint8_t conditional) {
  memset(guest_memory_pointer(node), 0, 0x38u);
  WR32(node + 0x28u, handler);
  WR32(node + 0x2cu, next);
  WR32(node + 0x30u, alternate);
  WR8(node + 0x34u, suspend);
  WR8(node + 0x35u, conditional);
}

static CPU fresh_cpu(void) {
  CPU cpu;

  memset(&cpu, 0, sizeof cpu);
  cpu.esp = STACK;
  return cpu;
}

static void build_graph(void) {
  uint32_t true_value = true_result();
  uint32_t false_value = false_result();

  memset(guest_memory_pointer(SCRIPT), 0, 0x400u);
  memset(guest_memory_pointer(CONTEXT), 0, 0x600u);
  WR32(SCRIPT, NODE_1);
  WR32(SCRIPT + 8u, 7u);
  WR32(SCRIPT + 0x1acu + 2u * 8u, 0x33u);
  WR32(CONTEXT + 4u, SCRIPT);
  WR32(CONTEXT + 0x0cu, INVALID_INDEX);
  WR32(CONTEXT + 0x170u + 4u, DESTINATION);
  WR8(NAME, 'x');
  WR8(NAME + 1u, 0u);
  WR32(DESTINATION, DESTINATION_VTABLE);
  WR32(DESTINATION_VTABLE + 0x18u, METHOD_ASSIGN);
  WR32(true_value, TRUE_VTABLE);
  WR32(false_value, FALSE_VTABLE);
  WR32(TRUE_VTABLE + 0x10u, METHOD_BOOL_TRUE);
  WR32(FALSE_VTABLE + 0x10u, METHOD_BOOL_FALSE);
  WR32(MANAGER + VALUE_ALLOCATED_BITS,
       (1u << VALUE_TRUE_INDEX) | (1u << VALUE_FALSE_INDEX));

  set_node(NODE_1, HANDLER_TRUE, NODE_2, 0u, 0u, 0u);
  WR32(NODE_1, NAME);
  WR32(NODE_1 + 4u, 0x99u);
  WR32(NODE_1 + 8u, 0x11u);
  WR32(NODE_1 + 0x0cu, 0x22u);
  WR32(NODE_1 + 0x24u, 2u);
  set_node(NODE_2, HANDLER_FALSE, NODE_3, NODE_4, 0u, 1u);
  set_node(NODE_3, HANDLER_FORBIDDEN, 0u, 0u, 0u, 0u);
  set_node(NODE_4, HANDLER_TRUE, NODE_5, 0u, 1u, 0u);
  WR32(NODE_4 + 8u, 0x44u);
  WR32(NODE_4 + 0x24u, 1u);
  set_node(NODE_5, HANDLER_TRUE, 0u, 0u, 0u, 0u);
}

int main(void) {
  CPU cpu;
  uint32_t prior = ARENA_BASE + 0x00350000u;
  uint32_t result;

  if (guest_memory_init() != 0 ||
      guest_memory_map_fixed(ARENA_BASE, ARENA_SIZE, PROT_READ | PROT_WRITE) !=
          0) {
    fprintf(stderr, "FAIL: could not map isolated guest arena\n");
    return 1;
  }
  build_graph();
  WR32(CURRENT_CONTEXT, prior);
  cpu = fresh_cpu();
  result = behaved_context_run(&cpu, CONTEXT);
  check(result == NODE_5,
        "suspension did not return the exact pending-node value");
  check(RD32(CONTEXT) == NODE_5,
        "suspension did not preserve the next authored node");
  check(RD32(CURRENT_CONTEXT) == prior,
        "context player did not restore its caller's current context");
  check(handler_true_calls == 2u && handler_false_calls == 1u,
        "first command batch followed the wrong graph edges");
  check(assignments == 1u, "named result was not assigned exactly once");
  check(releases == 3u,
        "first command batch did not release every pooled result");

  result = behaved_context_run(&cpu, CONTEXT);
  check(result == 1u && RD32(CONTEXT) == 0u,
        "resumed context did not complete from its pending node");
  check(handler_true_calls == 3u && releases == 4u,
        "resumed command did not execute and release its result");

  check(registered != NULL,
        "native context override constructor did not register");
  WR32(CONTEXT, 0u);
  WR32(SCRIPT, 0u);
  cpu = fresh_cpu();
  cpu.ecx = CONTEXT;
  WR32(cpu.esp, 0xdeadbeefu);
  registered(&cpu);
  check(cpu.eax == 1u && cpu.esp == STACK + 4u,
        "004d8b30 override did not reproduce its thiscall RET ABI");

  printf("BehavEd context player: %s -- native graph execution, argument "
         "delivery, result assignment, branching, suspension, and release\n",
         failures ? "FAILED" : "PASSED");
  return failures != 0;
}
