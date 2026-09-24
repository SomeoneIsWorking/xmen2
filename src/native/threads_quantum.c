/*
 * threads_quantum.c -- how many JIT steps (block entries, x86port's run
 * budget) a stretch of guest code that crosses no host boundary keeps its
 * turn for before guest_quantum() (threads.c) offers it up. A crossing offers
 * the turn anyway; this bounds only the code between crossings.
 */
#include "threads.h"
#include "x2_log.h"

#include <lucent/cvar_c.h>

static unsigned long g_quantum = 20000;

/*
 * X2_QUANTUM: JIT steps between preemptions. 0 disables it, which is
 * the CONTROL -- a scheduling change has to be measured against a build where
 * the mechanism is off, or "it stopped happening" is not evidence.
 */
void guest_quantum_from_env(void) {
  const unsigned long v = (unsigned long)lucent_cvar_number("quantum", 20000);
  if (!v) {
    g_quantum = 0u - 1ul; /* effectively never */
    x2_log_info("threads: X2_QUANTUM=0 -- preemption DISABLED. Two guest "
                "threads that both spin cannot take turns; this is the control "
                "for issue #57, not a configuration to run in.\n");
    return;
  }
  g_quantum = v;
  x2_log_info("threads: preemption quantum set to %lu JIT step(s).\n",
              g_quantum);
}

unsigned long guest_quantum_size(void) { return g_quantum; }
