#ifndef X2_X86_ENGINE_JIT_DIAG_H
#define X2_X86_ENGINE_JIT_DIAG_H

/*
 * The JIT's runtime diagnostic knobs, read from the layered CVar config and
 * applied to a freshly created engine, plus the one shutdown line that reports
 * what the verify cross-check actually covered.
 *
 *   jit.cache  (bool, default on)  -- off retranslates every block over the
 *                                     current guest bytes: the discriminator
 *                                     between a wrong emitter and a stale
 *                                     translation the guest has overwritten.
 *   jit.verify (bool, default off) -- shadow-interpret every block entry and
 *                                     stop at the first whole-machine
 *                                     divergence. Slow; for finding a wrong
 *                                     emitter against the real game.
 *
 * Its own file so x86_engine.c owns the run loop and the state bridge and not
 * this policy as well.
 */

struct X86pJitEngine;
struct X86pJitEngineStats;

/* Apply jit.cache / jit.verify to `jit`, logging any non-default choice. */
void x86_engine_jit_diag_configure(struct X86pJitEngine *jit);

/* The verify tally, printed at shutdown only when verify actually ran. */
void x86_engine_jit_diag_report(const struct X86pJitEngineStats *stats);

#endif /* X2_X86_ENGINE_JIT_DIAG_H */
