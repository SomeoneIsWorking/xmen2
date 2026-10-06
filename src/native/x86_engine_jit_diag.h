#ifndef X2_X86_ENGINE_JIT_DIAG_H
#define X2_X86_ENGINE_JIT_DIAG_H

/*
 * The JIT's runtime diagnostic knobs, read from the layered CVar config and
 * applied to a freshly created engine.
 *
 *   jit.cache  (bool, default on)  -- off retranslates every block over the
 *                                     current guest bytes: the discriminator
 *                                     between a wrong emitter and a stale
 *                                     translation the guest has overwritten.
 * Its own file so x86_engine.c owns the run loop and canonical CPU context, not
 * this policy as well.
 */

struct X86pJitEngine;
struct X86pJitEngineStats;

/* Apply product-safe JIT diagnostics, returning a reason on setup failure. */
int x86_engine_jit_diag_configure(struct X86pJitEngine *jit, char *reason,
                                  unsigned reason_len);

#endif /* X2_X86_ENGINE_JIT_DIAG_H */
