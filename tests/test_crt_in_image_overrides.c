#include "crt_in_image_overrides.h"

#include "x86rt.h"

#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "override_leaf.h"

int native_stubs_registered(const char *module, uint32_t linked_ep);
x86_override_leaf_fn native_stubs_leaf(const char *module, uint32_t linked_ep);

static jmp_buf fault_jmp;

void x87_fault(const char *what) {
  fprintf(stderr, "test_crt_in_image_overrides: x87_fault(%s)\n", what);
  longjmp(fault_jmp, 1);
}

static unsigned failures;

static void ftol2_run(CPU *c, int as_leaf) {
  if (!as_leaf) {
    x2_crt_ftol2(c);
    return;
  }
  const x86_override_leaf_fn leaf = native_stubs_leaf("XMen2.exe", 0x0067217cu);
  if (!leaf || !leaf(c)) {
    fprintf(stderr, "the _ftol2 leaf is missing or declined\n");
    failures++;
  }
}

static void ftol2_case_as(long double in, int64_t want, int as_leaf) {
  CPU c;
  cpu_reset(&c);
  c.reg[kX86pEsp] = 0x2000u;
  x87_push(&c, in);

  ftol2_run(&c, as_leaf);

  int64_t got = (int64_t)(((uint64_t)c.reg[kX86pEdx] << 32) | c.reg[kX86pEax]);
  if (got != want) {
    fprintf(stderr, "ftol2(%.4Lf): got %lld, want %lld\n", in, (long long)got,
            (long long)want);
    failures++;
  }
  if (c.reg[kX86pEsp] != 0x2004u) {
    fprintf(stderr,
            "ftol2(%.4Lf): esp %08x, want 00002004 (cdecl pops the "
            "return address only)\n",
            in, c.reg[kX86pEsp]);
    failures++;
  }
  if (x86p_x87_depth(&c.x87) != 0) {
    fprintf(stderr, "ftol2(%.4Lf): x87 depth %d, want 0 (ST(0) consumed)\n", in,
            x86p_x87_depth(&c.x87));
    failures++;
  }
}

/* The override and the leaf a direct CALL runs instead give one answer. */
static void ftol2_case(long double in, int64_t want) {
  ftol2_case_as(in, want, 0);
  ftol2_case_as(in, want, 1);
}

int main(void) {
  if (setjmp(fault_jmp)) {
    fprintf(stderr, "an x87 fault reached the test\n");
    return 1;
  }

  /* Truncates toward zero, both signs, whatever the fraction. */
  ftol2_case(3.0L, 3);
  ftol2_case(3.9L, 3);
  ftol2_case(-3.9L, -3);
  ftol2_case(0.0L, 0);
  ftol2_case(-0.9L, 0);

  /* Wider than 32 bits: EDX must carry the high word. */
  ftol2_case(4294967296.0L, 4294967296LL);
  ftol2_case(-4294967297.0L, -4294967297LL);

  if (!native_stubs_registered("XMen2.exe", 0x0067217cu)) {
    fprintf(stderr, "the constructor did not register the _ftol2 override\n");
    failures++;
  }

  if (failures) {
    fprintf(stderr, "%u failure(s)\n", failures);
    return 1;
  }
  puts("crt_in_image_overrides: ok");
  return 0;
}
