/* x87_exact.c -- see x87_exact.h. */
#include "x87_exact.h"

#include "x2_log.h"

#include <lucent/cvar_c.h>

#include <stdint.h>
#include <stdlib.h>

int x87_exact_host(void) {
#if X86P_EXACT_LONG_DOUBLE && (defined(__x86_64__) || defined(__i386__))
  uint16_t control;
  __asm__ volatile("fnstcw %0" : "=m"(control));
  return control == X86P_X87_CW_INIT;
#else
  return 0;
#endif
}

int x87_guest_order_applies(const X86pX87 *x87, unsigned pushes) {
  if (x87->control != X86P_X87_CW_INIT) {
    return 0;
  }
  for (unsigned i = 1; i <= pushes; i++) {
    if (x87->tag[(x87->top - i) & 7u] != (uint8_t)kX86pX87TagEmpty) {
      return 0;
    }
  }
  return 1;
}

int x87_exact_for_guest(const X86pX87 *x87, unsigned pushes) {
  return x87_guest_order_applies(x87, pushes) && x87_exact_host();
}

int x87_verify_requested(const char *cvar) {
  if (!lucent_cvar_flag(cvar, 0)) {
    return 0;
  }
  if (!x87_exact_host()) {
    x2_log_error("%s: this host's native answers are not the guest's bits "
                 "(long double is not the x87 format, or the FPU is not at "
                 "the guest's control word), so they cannot be verified "
                 "against the guest body here; not continuing.\n",
                 cvar);
    abort();
  }
  return 1;
}
