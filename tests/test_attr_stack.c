/*
 * Unit tests for igAttrStack::customReset and igAttrStackManager::reset native overrides.
 */
#include "attr_stack.h"
#include "attr_stack_verify.h"
#include "guest_memory.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

int native_stubs_registered(const char *module, uint32_t linked_ep);

void attr_stack_verify_begin(AttrStackVerify *v, uint32_t self) {
  (void)self;
  memset(v, 0, sizeof *v);
}
void attr_stack_verify_end(const CPU *C, AttrStackVerify *v, uint32_t self) {
  (void)C;
  (void)v;
  (void)self;
}

/* Stubs for x86_guest_body and x86_guest_call_args in test */
void x86_guest_body(CPU *C, const char *module, uint32_t linked_ep) {
  (void)C;
  (void)module;
  (void)linked_ep;
}

void x86_guest_call_args(CPU *C, uint32_t target, uint32_t callee_pop_bytes) {
  (void)C;
  (void)target;
  (void)callee_pop_bytes;
}

int x86_override_resolve_check(const char *module, uint32_t linked_ep,
                               uint32_t *mapped_out, char *why, size_t whyn) {
  (void)module;
  (void)linked_ep;
  (void)mapped_out;
  (void)why;
  (void)whyn;
  return 1;
}

enum {
  ARENA = 0x33000000u,
  ARENA_SIZE = 0x00100000u,
  STACK1 = ARENA + 0x1000u,
  STACK2 = ARENA + 0x1100u,
  STACK3 = ARENA + 0x1200u,
  MGR = ARENA + 0x2000u,
  LIST = ARENA + 0x2100u,
  ARRAY = ARENA + 0x2200u,
  P18 = ARENA + 0x3000u,
  P1C = ARENA + 0x3100u,
  P2C = ARENA + 0x3200u,
  P40 = ARENA + 0x3300u,
};

static unsigned failures = 0;

#define CHECK(cond, msg)                                                       \
  do {                                                                         \
    if (!(cond)) {                                                             \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, msg);            \
      failures++;                                                              \
    }                                                                          \
  } while (0)

static void test_custom_reset(void) {
  WR32(STACK1 + 0x08u, 42u);
  WR32(STACK1 + 0x14u, 0x12345678u);
  WR32(STACK1 + 0x18u, 10u);
  WR32(STACK1 + 0x24u, 0u);
  WR8(STACK1 + 0x20u, 1u);
  WR8(STACK1 + 0x28u, 2u);
  WR32(STACK1 + 0x30u, 99u);

  CPU C;
  memset(&C, 0, sizeof C);
  C.ecx = STACK1;
  C.esp = 0x1000u;
  x2_override_10034d10(&C);
  CHECK(C.esp == 0x1004u, "customReset did not pop return address");

  CHECK(RD32(STACK1 + 0x08u) == 0u, "f08 not 0");
  CHECK(RD32(STACK1 + 0x18u) == 0xffffffffu, "f18 not -1");
  CHECK(RD32(STACK1 + 0x24u) == 0x12345678u, "f24 not f14");
  CHECK(RD8(STACK1 + 0x20u) == 0u, "f20 not 0");
  CHECK(RD8(STACK1 + 0x28u) == 0u, "f28 not 0");
  CHECK(RD32(STACK1 + 0x30u) == 0u, "f30 not 0");
}

static void test_manager_reset(void) {
  /* Set up 3 stacks */
  WR32(STACK1 + 0x14u, 0x11111111u);
  WR32(STACK1 + 0x08u, 5u);
  WR32(STACK2 + 0x14u, 0x22222222u);
  WR32(STACK2 + 0x08u, 6u);
  WR32(STACK3 + 0x14u, 0x33333333u);
  WR32(STACK3 + 0x08u, 7u);

  WR32(ARRAY + 0u, STACK1);
  WR32(ARRAY + 4u, STACK2);
  WR32(ARRAY + 8u, STACK3);

  WR32(LIST + 0x10u, ARRAY);

  WR32(MGR + 0x0cu, 3u); /* count */
  WR32(MGR + 0x10u, LIST);
  WR32(MGR + 0x18u, P18);
  WR32(MGR + 0x1cu, P1C);
  WR32(MGR + 0x2cu, P2C);
  WR32(MGR + 0x40u, P40);

  WR32(P18 + 0x8u, 100u);
  WR32(P1C + 0x8u, 200u);
  WR32(P2C + 0x8u, 300u);
  WR32(P40 + 0x14u, 400u);

  CPU C;
  memset(&C, 0, sizeof C);
  C.ecx = MGR;
  C.esp = 0x2000u;
  x2_override_10034d30(&C);
  CHECK(C.esp == 0x2004u, "manager reset did not pop return address");

  /* Check stack 1 */
  CHECK(RD32(STACK1 + 0x08u) == 0u, "stack 1 f08");
  CHECK(RD32(STACK1 + 0x24u) == 0x11111111u, "stack 1 f24");
  /* Check stack 2 */
  CHECK(RD32(STACK2 + 0x08u) == 0u, "stack 2 f08");
  CHECK(RD32(STACK2 + 0x24u) == 0x22222222u, "stack 2 f24");
  /* Check stack 3 */
  CHECK(RD32(STACK3 + 0x08u) == 0u, "stack 3 f08");
  CHECK(RD32(STACK3 + 0x24u) == 0x33333333u, "stack 3 f24");

  /* Check post-loop fields */
  CHECK(RD32(P18 + 0x8u) == 0u, "P18 f8 not 0");
  CHECK(RD32(P1C + 0x8u) == 0u, "P1C f8 not 0");
  CHECK(RD32(P2C + 0x8u) == 0u, "P2C f8 not 0");
  CHECK(RD32(P40 + 0x14u) == 0u, "P40 f14 not 0");
}

int main(void) {
  if (guest_memory_init() != 0 ||
      guest_memory_map_fixed(ARENA, ARENA_SIZE, PROT_READ | PROT_WRITE) != 0) {
    fprintf(stderr, "could not map test arena\n");
    return 1;
  }

  test_custom_reset();
  test_manager_reset();

  if (!native_stubs_registered("libIGSg.dll", 0x10034d10u)) {
    fprintf(stderr, "constructor did not register 0x10034d10\n");
    failures++;
  }
  if (!native_stubs_registered("libIGSg.dll", 0x10034d30u)) {
    fprintf(stderr, "constructor did not register 0x10034d30\n");
    failures++;
  }

  if (failures) {
    fprintf(stderr, "%u failure(s)\n", failures);
    return 1;
  }
  puts("attr_stack: ok");
  return 0;
}
