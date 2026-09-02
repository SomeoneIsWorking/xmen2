/*
 * x86_diag_dump.c -- everything worth reading on a stop path, in one call.
 *
 * Called from every abort: a missing body, a stack-contract violation, a
 * fault, an engine refusal. It is an AGGREGATOR and owns none of the reports
 * it calls -- which is why it does not belong in x86rt_native.c, whose subject
 * is the module, thunk and override tables.
 *
 * Order matters: where execution is comes before what it did, and the boundary
 * ring is last because it is the longest.
 */
#include "x86rt.h"
#include "x86rt_native.h"
#include "x86_engine.h"
#include "x86_reached.h"

void x86_diag_dump(void)
{
    /*
     * The THREAD table, on every stop path. It is registered with atexit()
     * too, and atexit does not run on abort() -- so at exactly the stops worth
     * reading (a stall, a missing import, a fault) the one fact that explains
     * a stalled run, "tid N is suspended and nobody resumed it", was silent.
     */
    /* The thread and critical-section reports are NOT printed here: they moved
       to x2_interrupt_reports, which runs on every ending rather than only on
       the ones that dump the ring. Printing them in both places would double
       every number on a killed run. */
    /* The multimedia timers, for the same reason: a stall whose cause is "the
       callback that would have ended this wait has never run" is invisible
       unless the fire count is printed where the stall is. */
    { extern void winmm_report(void); winmm_report(); }
    x2_engine_where();
    x86_peek_report();
    x86_reached_report();
#ifdef X86_NATIVE_TRACE
    x86_args_report();
#endif
    x86_ring_dump();
}
