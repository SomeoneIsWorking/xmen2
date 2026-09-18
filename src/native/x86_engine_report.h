/*
 * x86_engine_report.h -- the engine's live heartbeat report.
 *
 * Separate from the engine because it owns a different thing: the engine runs
 * guest code, this reads counters and decides what a five-second line has to
 * say for the numbers in it to be readable. It holds the pending-request flag
 * because the request and the report are the same responsibility seen from the
 * two threads that share it.
 */
#ifndef X2_X86_ENGINE_REPORT_H
#define X2_X86_ENGINE_REPORT_H

#include "x86_engine_jit_pool.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Ask for a snapshot at the next guest boundary, and say whether one was
 * already pending. The heartbeat thread cannot read JIT state itself -- only
 * the guest-lock owner may -- so an unanswered request is how the heartbeat
 * reports that no boundary ran.
 */
int x86_engine_report_request(void);

/*
 * Emit the report if one was asked for, and clear the request. Called from the
 * guest boundary, with the pool and the crossing count the engine owns.
 */
void x86_engine_report_live_if_requested(const X86EngineJitPool *jit,
                                         unsigned long callouts);

#ifdef __cplusplus
}
#endif

#endif
