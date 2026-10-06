/*
 * override_leaf.c: which thunks the resolver answers with a leaf, what the
 * thunk leaf runs, and that the contract guard aborts inside a leaf and
 * nowhere else.
 */
#include "override_leaf.h"

#include "guest_memory.h"
#include "x86_import_fastpath.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

enum {
  NAMED_SLOT = 3u,    /* a bound thunk this test registers as a leaf */
  UNREGISTERED = 4u,  /* a bound thunk nobody registers */
  FASTPATH_SLOT = 5u, /* a leaf-safe fast-path import */
  NAMED_OVERRIDE = 0x00401000u,
  ARENA = 0x30000000u,
  ARENA_SIZE = 0x00010000u,
  STUB = ARENA + 0x100u, /* JMP [SLOT] */
  SLOT = ARENA + 0x200u,
  NOT_A_STUB = ARENA + 0x300u,
};

static uint32_t thunk_at(uint32_t slot) { return THUNK_BASE + slot * 16u; }

static unsigned failures;
static uint32_t s_thunk_called;
static uint32_t s_fastpath_called;
static int s_thunk_answer = 1;
static int s_thunk_breaks_contract;

/* ---- the runtime this module sits on ---------------------------------- */

/* X2_WRITE_WATCH, unarmed: the test's own stores never fire it. */
volatile uint32_t x2_write_watch_addr;

void x2_write_watch_fire(uint32_t address, uint32_t value) {
  (void)address;
  (void)value;
}

const char *x86_thunk_name(uint32_t addr, const char **module_out) {
  if (addr == thunk_at(NAMED_SLOT) || addr == thunk_at(UNREGISTERED)) {
    *module_out = "IFoo";
    return addr == thunk_at(NAMED_SLOT) ? "Bar" : "Baz";
  }
  return NULL;
}

int x86_override_at(uint32_t addr, const char **module, uint32_t *linked_ep) {
  (void)addr;
  (void)module;
  (void)linked_ep;
  return 0;
}

int x86_native_thunk_call(uint32_t addr, CPU *C) {
  s_thunk_called = addr;
  if (s_thunk_breaks_contract) {
    x86_override_leaf_forbid("called guest code");
  }
  if (s_thunk_answer) {
    C->reg[kX86pEsp] += 8u;
  }
  return s_thunk_answer;
}

int x86_import_fastpath_leaf_safe(uint32_t addr) {
  return addr == thunk_at(FASTPATH_SLOT);
}

int x86_import_fastpath_dispatch(CPU *cpu) {
  s_fastpath_called = cpu->eip;
  cpu->reg[kX86pEsp] += 4u;
  return 1;
}

/* ---- cases ------------------------------------------------------------ */

static void expect(int ok, const char *what) {
  if (!ok) {
    fprintf(stderr, "test_override_leaf: %s\n", what);
    failures++;
  }
}

static int call_leaf(uint32_t target, CPU *c) {
  const X86pJitLeafFn leaf = x86_override_leaf_at(target, NULL);
  memset(c, 0, sizeof *c);
  c->eip = target;
  c->reg[kX86pEsp] = 0x1000u;
  return leaf ? leaf(c) : -1;
}

static void test_the_resolver_answers_registered_and_fastpath_thunks(void) {
  expect(x86_override_leaf_at(thunk_at(NAMED_SLOT), NULL) != NULL,
         "a registered thunk has no leaf");
  expect(x86_override_leaf_at(thunk_at(UNREGISTERED), NULL) == NULL,
         "an unregistered thunk has a leaf");
  expect(x86_override_leaf_at(thunk_at(FASTPATH_SLOT), NULL) != NULL,
         "a leaf-safe fast-path import has no leaf");
  expect(x86_override_leaf_at(NAMED_OVERRIDE, NULL) == NULL,
         "an address with no override has a leaf");
}

static void test_a_thunk_leaf_runs_the_dispatchers_path(void) {
  CPU c;
  expect(call_leaf(thunk_at(NAMED_SLOT), &c) == 1, "the thunk leaf declined");
  expect(s_thunk_called == thunk_at(NAMED_SLOT),
         "the thunk leaf did not dispatch its own target");
  expect(c.reg[kX86pEsp] == 0x1008u, "the thunk leaf lost the stub's pops");

  s_thunk_answer = 0;
  expect(call_leaf(thunk_at(NAMED_SLOT), &c) == 0,
         "a thunk leaf completed a call its stub refused");
  expect(c.reg[kX86pEsp] == 0x1000u, "a declined thunk leaf changed ESP");
  s_thunk_answer = 1;

  expect(call_leaf(thunk_at(FASTPATH_SLOT), &c) == 1,
         "the fast-path leaf declined");
  expect(s_fastpath_called == thunk_at(FASTPATH_SLOT),
         "the fast-path leaf did not run the fast path");
}

static void write_stub(uint32_t at, uint32_t slot) {
  WR32(at, 0x25ffu | (slot << 16));
  WR32(at + 4u, slot >> 16);
}

/* A direct CALL to the linker's `JMP [slot]` stub runs the slot's thunk leaf,
   following the slot as it is on each call. */
static void test_an_import_stub_runs_its_slots_thunk_leaf(void) {
  CPU c;
  write_stub(STUB, SLOT);
  WR32(SLOT, thunk_at(NAMED_SLOT));
  WR32(NOT_A_STUB, 0x90909090u);
  WR32(NOT_A_STUB + 4u, 0x90909090u);
  expect(x86_override_leaf_at(STUB, NULL) != NULL,
         "a stub to a leaf-safe thunk has no leaf");
  expect(x86_override_leaf_at(NOT_A_STUB, NULL) == NULL,
         "code that is no stub has a leaf");
  expect(x86_override_leaf_at(ARENA + ARENA_SIZE, NULL) == NULL,
         "an unmapped target has a leaf");

  s_thunk_called = 0;
  expect(call_leaf(STUB, &c) == 1, "the stub leaf declined");
  expect(s_thunk_called == thunk_at(NAMED_SLOT),
         "the stub leaf did not dispatch its slot's thunk");
  expect(c.reg[kX86pEsp] == 0x1008u, "the stub leaf lost the stub's pops");

  const X86pJitLeafFn leaf = x86_override_leaf_at(STUB, NULL);
  WR32(SLOT, thunk_at(UNREGISTERED));
  memset(&c, 0, sizeof c);
  c.eip = STUB;
  c.reg[kX86pEsp] = 0x1000u;
  s_thunk_called = 0;
  expect(leaf(&c) == 0, "a rebound slot was trusted from translation");
  expect(s_thunk_called == 0 && c.eip == STUB && c.reg[kX86pEsp] == 0x1000u,
         "a declined stub leaf changed the CPU");
  expect(x86_override_leaf_at(STUB, NULL) == NULL,
         "a stub to an unregistered thunk has a leaf");
}

/* The guard is silent outside a leaf, including after one returns. */
static void test_the_guard_is_silent_outside_a_leaf(void) {
  CPU c;
  x86_override_leaf_forbid("released the guest lock");
  (void)call_leaf(thunk_at(NAMED_SLOT), &c);
  x86_override_leaf_forbid("released the guest lock");
}

/* Inside one, it aborts. Run in a child, since that is the process's end. */
static void test_the_guard_aborts_inside_a_leaf(void) {
  fflush(NULL);
  const pid_t child = fork();
  if (child == 0) {
    CPU c;
    s_thunk_breaks_contract = 1;
    (void)call_leaf(thunk_at(NAMED_SLOT), &c);
    _exit(0);
  }
  int status = 0;
  expect(child > 0 && waitpid(child, &status, 0) == child,
         "could not run the guard's child");
  expect(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT,
         "a leaf that called guest code did not abort");
}

int main(void) {
  if (guest_memory_init() != 0 ||
      guest_memory_map_fixed(ARENA, ARENA_SIZE, PROT_READ | PROT_WRITE) != 0) {
    fprintf(stderr, "could not map the test arena\n");
    return 1;
  }
  x86_register_thunk_leaf(thunk_at(NAMED_SLOT));
  test_the_resolver_answers_registered_and_fastpath_thunks();
  test_a_thunk_leaf_runs_the_dispatchers_path();
  test_an_import_stub_runs_its_slots_thunk_leaf();
  test_the_guard_is_silent_outside_a_leaf();
  test_the_guard_aborts_inside_a_leaf();
  if (failures) {
    fprintf(stderr, "%u failure(s)\n", failures);
    return 1;
  }
  puts("override_leaf: ok");
  return 0;
}
