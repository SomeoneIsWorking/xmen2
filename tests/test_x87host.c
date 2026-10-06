#include "x87host.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

void x87_fault(const char *what) {
  fprintf(stderr, "unexpected x87 fault: %s\n", what);
  __builtin_trap();
}

int main(void) {
  CPU C;
  cpu_reset(&C);

  x87_host_begin(&C);
  x87_host_end(&C);
  if (x86p_x87_depth(&C.x87) != 0) {
    fprintf(stderr, "empty host x87 stack produced %d value(s)\n",
            x86p_x87_depth(&C.x87));
    return 1;
  }

  x87_host_begin(&C);
  __asm__ __volatile__("fld1");
  x87_host_end(&C);
  long double result = 0.0L;
  const int depth = x86p_x87_depth(&C.x87);
  if (depth != 1 || !x86p_x87_get(&C.x87, 0, &result) ||
      fabsl(result - 1.0L) > 1e-12L) {
    fprintf(stderr, "ST0 return was not captured: depth=%d value=%Lf\n", depth,
            depth ? result : 0.0L);
    return 1;
  }
  return 0;
}
