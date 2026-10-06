/*
 * Where block dispatch goes: the chain census (how many dispatches a chaining
 * backend would remove) and the block-entry histogram (which blocks are hot).
 *
 * Both are armed diagnostics and silent otherwise; the heartbeat and the
 * shutdown report call them with a line prefix.
 */
#ifndef X2_X86_ENGINE_DISPATCH_REPORT_H
#define X2_X86_ENGINE_DISPATCH_REPORT_H

#include "x86_engine_jit_pool.h"

#include "jit_profile.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Issue #166: of the dispatches actually paid, how many a chaining backend
   would have removed. Silent unless jit.chain armed the census. */
void x86_engine_report_chain_census(const X86EngineJitPool *jit,
                                    const char *tag);

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
