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

#include "jit_profile.h"

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

/*
 * The hottest translated blocks, when `jit.profile` armed the histogram.
 *
 * Printed by the heartbeat as well as at shutdown, and that is the whole
 * reason it is a function. The browser has no shutdown: a page is closed, the
 * worker is torn down, and nothing runs afterwards -- so an instrument that
 * only speaks on the way out is an instrument the browser target does not
 * have, which is exactly where the guest is slowest and the question is
 * loudest. `tag` prefixes each line ("[HB] " from the heartbeat, "" at
 * shutdown) so a reader can tell a running snapshot from the final one.
 *
 * Silent when the histogram was not armed, because the arming decision is
 * already reported once at startup and repeating it every five seconds would
 * bury the lines that change. NOT silent when it is armed and empty: that is
 * the case worth hearing about, and an absent table cannot be told from an
 * instrument that never ran.
 */
void x86_engine_report_hot_blocks(const X86EngineJitPool *jit, const char *tag);

/*
 * The same report, over a histogram the caller already holds.
 *
 * The seam a falsifier needs. Reaching the armed-and-empty case through the
 * pool would mean standing up an engine, a memory map and a guest image just
 * to prove a table prints a sentence when it has no rows -- and the case that
 * matters most is the one a real run is least likely to produce, so it would
 * stay untested exactly where silence is the failure. A test builds the
 * profile directly and runs THIS function, which is the one the product runs.
 */
void x86_engine_report_hot_blocks_from(const X86pJitProfile *profile,
                                       const char *tag);

#ifdef __cplusplus
}
#endif

#endif
