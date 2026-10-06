#include "x86_engine_x87_precision.h"

#include "cpu.h"
#include "x87.h"

#include <lucent/cvar_c.h>
#include <lucent/log_c.h>

/* -1 until the cvar has been read, as the census does: a CPU can be built
   before any engine configuration runs. */
static int g_double = -1;

static int wanted(void) {
  if (g_double < 0) {
    g_double =
        lucent_cvar_flag("x87.double", 1) && x86p_x87_double_arith_available();
    lucent_log_info(
        "engine", "x87 arithmetic: %s",
        g_double ? "binary64 (x87.double=1, and this host has no x87 unit)"
        : x86p_x87_double_arith_available()
            ? "extended (x87.double=0)"
            : "this host's own (x87.double does not apply here)");
  }
  return g_double;
}

void x86_engine_x87_precision_attach(struct X86pCpu *cpu) {
  if (cpu) {
    x86p_x87_set_double_arith(&cpu->x87, wanted());
  }
}
