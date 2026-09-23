/* x87_exact.c -- see x87_exact.h. */
#include "x87_exact.h"

#include <stdint.h>

int x87_exact_host(void) {
#if X86P_EXACT_LONG_DOUBLE && (defined(__x86_64__) || defined(__i386__))
  uint16_t control;
  __asm__ volatile("fnstcw %0" : "=m"(control));
  return control == X86P_X87_CW_INIT;
#else
  return 0;
#endif
}

int x87_exact_for_guest(const X86pX87 *x87, unsigned pushes) {
  if (x87->control != X86P_X87_CW_INIT) {
    return 0;
  }
  for (unsigned i = 1; i <= pushes; i++) {
    if (x87->tag[(x87->top - i) & 7u] != (uint8_t)kX86pX87TagEmpty) {
      return 0;
    }
  }
  return x87_exact_host();
}
