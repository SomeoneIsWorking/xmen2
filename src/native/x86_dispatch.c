/*
 * x86_dispatch.c -- which engine runs the body at a guest address.
 *
 * ONE OWNER, TWO ANSWERS. Every indirect guest call arrives here with a mapped
 * address and nothing else. A registered native import/override runs natively;
 * every other address enters the runtime JIT. Failure to execute is terminal.
 */
#include "x86_dispatch_report.h"
#include "x86_engine.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include <stdlib.h>

static void x86_dispatch_one(CPU *C, uint32_t target) {
  /*
   * Host code first: an import thunk or a native override is a C function
   * at a guest address, and there are no guest bytes there to interpret.
   * Everything else is the guest's own code, and the engine is the only
   * thing in this binary that can run it.
   */
  if (x86_native_call_at(target, C))
    return;
  if (x2_engine_call(target, C))
    return;
  x86_report_missing_body(C, target);
  abort();
}

void x86_dispatch(CPU *C, uint32_t target) { x86_dispatch_one(C, target); }
