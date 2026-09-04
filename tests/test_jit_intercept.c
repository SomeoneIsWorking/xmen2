/*
 * The JIT interception predicates (src/native/x86_engine_intercept.c).
 *
 * Regression for issue #140's follow-up: x86_engine_jit_intercept gated the
 * native-override check on x86_guest_call_top() returning a frame. The old
 * fixed-depth shadow array dropped that pointer during deep boot nesting.
 * The JIT then translated override entry points as raw guest x86: under
 * engine=jit the font-scaling, boot-splash, prompt-glyph and native-FMV
 * overrides never fired, and the intro played through the guest's own MMX
 * decoder at a fraction of the interpreter's speed.
 *
 * The address check must hold regardless of frame-stack depth.
 */
#include "x86_engine_intercept.h"
#include "x86_guest_call_stack.h"
#include "x86rt_native.h"

#include "cpu.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#define OVERRIDE_BODY 0x00596af0u /* a resolved native override entry point */
#define PLAIN_GUEST 0x00401234u   /* ordinary guest code, run in place */
#define THUNK_ADDR 0x000c0010u    /* an import thunk (THUNK_BASE + 1 slot) */

X2_INTERNAL uint32_t g_override_bloom[64];

/* Stand in for x86rt_native.c: the override resolver would have inserted
   OVERRIDE_BODY into the owned set. */
int x86_native_body_at(uint32_t addr) { return addr == OVERRIDE_BODY; }
int x86_setjmp3_thunk(uint32_t addr) {
  (void)addr;
  return 0;
}

static int checks;
#define CHECK(c)                                                               \
  do {                                                                         \
    if (!(c)) {                                                                \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);             \
      assert(c);                                                               \
    }                                                                          \
    checks++;                                                                  \
  } while (0)

static X86pCpu at(uint32_t eip) {
  X86pCpu c;
  c.eip = eip;
  c.reg[kX86pEsp] = 0x00300000u;
  return c;
}

static void addr_predicate_is_frame_independent(void) {
  X86GuestCallFrame frames[69];
  X86pCpu body = at(OVERRIDE_BODY);
  X86pCpu plain = at(PLAIN_GUEST);
  X86pCpu thunk = at(THUNK_ADDR);

  CHECK(x86_guest_call_depth() == 0);

  /* No frame at all: still a hand-back. */
  CHECK(x86_engine_jit_intercept(&body, NULL) == 1);
  CHECK(x86_engine_jit_intercept(&thunk, NULL) == 1);
  CHECK(x86_engine_jit_intercept(&plain, NULL) == 0);

  /* A normal, shallow frame -- unchanged. */
  x86_guest_call_push(&frames[0], NULL, 0x00402000u, 0x00402005u, 0x00320000u);
  CHECK(x86_engine_jit_intercept(&body, NULL) == 1);
  CHECK(x86_engine_jit_intercept(&plain, NULL) == 0);

  /* Deep nesting remains fully represented and cannot suppress hand-back. */
  for (int i = 1; i < 69; i++)
    x86_guest_call_push(&frames[i], NULL, 0x00402000u + (uint32_t)i, 0u,
                        0x00320000u);
  CHECK(x86_guest_call_depth() == 69u);
  CHECK(x86_guest_call_top() == &frames[68]);
  CHECK(x86_engine_jit_intercept(&body, NULL) == 1); /* was 0 -- the bug */
  CHECK(x86_engine_jit_intercept(&thunk, NULL) == 1);
  CHECK(x86_engine_jit_intercept(&plain, NULL) == 0);

  for (int i = 68; i >= 0; i--)
    x86_guest_call_pop(&frames[i]);
  CHECK(x86_guest_call_depth() == 0);

  /* x86_engine_intercepts_addr and the boundary predicate agree with it. */
  CHECK(x86_engine_intercepts_addr(OVERRIDE_BODY) == 1);
  CHECK(x86_engine_intercepts_addr(PLAIN_GUEST) == 0);
  CHECK(x86_engine_jit_boundary(OVERRIDE_BODY, NULL) == 1);
  CHECK(x86_engine_jit_boundary(PLAIN_GUEST, NULL) == 0);
}

static void selftest_entry_is_still_run_in_place(void) {
  X86GuestCallFrame frame;
  /* The engine selftest enters a body at its own frame entry to run it both
     ways and compare; that one arrival must NOT be intercepted. */
  x86_guest_call_push(&frame, NULL, OVERRIDE_BODY, 0x00402005u, 0x00320000u);
  X86pCpu at_entry = at(OVERRIDE_BODY);
  CHECK(x86_engine_jit_intercept(&at_entry, NULL) == 0);
  x86_guest_call_pop(&frame);
}

int main(void) {
  x86_override_bloom_add(OVERRIDE_BODY);
  addr_predicate_is_frame_independent();
  selftest_entry_is_still_run_in_place();
  printf("test_jit_intercept: %d check(s) passed\n", checks);
  return 0;
}
