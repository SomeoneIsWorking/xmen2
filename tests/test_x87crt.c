#include "x87crt.h"

#include <math.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>

static jmp_buf fault_jmp;
static const char *fault_text;

void x87_fault(const char *what) {
  fault_text = what;
  longjmp(fault_jmp, 1);
}

static int positive(void) {
  CPU c;
  cpu_reset(&c);
  c.reg[kX86pEsp] = 0x1000;
  x87_crt_push(&c, 2.0L);
  x87_crt_push(&c, 8.0L);
  x87_crt_cipow(&c);
  long double result = 0.0L;
  int depth = x86p_x87_depth(&c.x87);
  if (depth != 1 || c.reg[kX86pEsp] != 0x1004 ||
      !x86p_x87_get(&c.x87, 0, &result) || fabsl(result - 256.0L) > 1e-12L) {
    fprintf(stderr, "x87crt positive: depth=%d esp=%08x result=%.18Lg\n", depth,
            c.reg[kX86pEsp], result);
    return 1;
  }
  puts("x87crt positive: _CIpow consumed 2 operands and produced 1 result");
  x87_crt_push(&c, -17.75L);
  x87_crt_ftol(&c);
  depth = x86p_x87_depth(&c.x87);
  if (depth != 1 || c.reg[kX86pEsp] != 0x1008 ||
      c.reg[kX86pEax] != 0xffffffefu || c.reg[kX86pEdx] != 0xffffffffu) {
    fprintf(stderr,
            "x87crt positive: ftol depth=%d esp=%08x edx:eax=%08x:%08x\n",
            depth, c.reg[kX86pEsp], c.reg[kX86pEdx], c.reg[kX86pEax]);
    return 1;
  }
  puts("x87crt positive: ftol consumed 1 operand and returned EDX:EAX");
  /* The guest's control word rounds to nearest; _ftol still truncates. */
  c.x87.control = (uint16_t)(c.x87.control & ~X86P_X87_RC_MASK);
  x87_crt_push(&c, 2.75L);
  x87_crt_ftol(&c);
  if (c.reg[kX86pEax] != 2u || c.reg[kX86pEdx] != 0u) {
    fprintf(stderr,
            "x87crt positive: ftol(2.75) under RC=nearest gave %08x:%08x\n",
            c.reg[kX86pEdx], c.reg[kX86pEax]);
    return 1;
  }
  x87_crt_push(&c, 0x1p63L);
  x87_crt_ftol(&c);
  if (c.reg[kX86pEax] != 0u || c.reg[kX86pEdx] != 0x80000000u) {
    fprintf(stderr,
            "x87crt positive: ftol(2^63) gave %08x:%08x, not the indefinite\n",
            c.reg[kX86pEdx], c.reg[kX86pEax]);
    return 1;
  }
  puts("x87crt positive: ftol truncates under any RC and stores the indefinite "
       "out of range");
  return 0;
}

static int negative(void) {
  CPU c;
  cpu_reset(&c);
  fault_text = NULL;
  if (!setjmp(fault_jmp)) {
    (void)x87_crt_pop(&c);
    fputs("x87crt negative: empty stack was silently accepted\n", stderr);
    return 1;
  }
  if (!fault_text || !strstr(fault_text, "underflow")) {
    fprintf(stderr, "x87crt negative: wrong fault: %s\n",
            fault_text ? fault_text : "none");
    return 1;
  }
  puts("x87crt negative: empty stack was rejected as underflow");
  return 0;
}

int main(void) { return positive() || negative(); }
