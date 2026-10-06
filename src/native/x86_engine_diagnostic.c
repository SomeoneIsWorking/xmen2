/*
 * x86port's diagnostics enter this port's logger here.
 *
 * The library reports a violated contract -- an unsupported flag combination,
 * a JIT invariant that does not hold, a malformed decode -- through one sink
 * and then aborts. Its default sink writes to standard error, which no host of
 * this port reads: in the browser a worker's standard error never reaches the
 * page at all. A run that stops this way therefore says "native code called
 * abort()" and nothing about why, which is exactly what it did until this sink
 * was installed.
 *
 * The sink is one process-wide value the library never writes itself, so it is
 * configured once, before any execution thread exists.
 */
#include "x86_engine_diagnostic.h"

#include "x2_log.h"
#include "x86rt_native.h"

#include "diagnostic.h"

static void engine_diagnostic_sink(const X86pDiagnostic *diagnostic,
                                   void *user) {
  const char *component = "library";
  const char *message = "unspecified diagnostic";
  (void)user;
  if (diagnostic != NULL) {
    if (diagnostic->component != NULL)
      component = diagnostic->component;
    if (diagnostic->message != NULL)
      message = diagnostic->message;
  }
  if (diagnostic != NULL && diagnostic->level == kX86pDiagnosticFatal) {
    x2_log_error("x86port[%s]: fatal: %s\n", component, message);
    /* The library aborts the moment this returns, so this is the last place
       that can still say where the guest was. */
    x86_diag_dump();
    return;
  }
  x2_log_error("x86port[%s]: %s\n", component, message);
}

void x86_engine_diagnostic_install(void) {
  x86p_diagnostic_set_sink(engine_diagnostic_sink, NULL);
}
