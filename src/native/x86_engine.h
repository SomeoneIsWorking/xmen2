/*
 * x86_engine.h -- X-Men 2's product boundary to x86port's runtime JIT.
 *
 * There is one gameplay executor. Consumer interception hooks in x86port's JIT
 * allow host import thunks, native overrides, setjmp frames, and return
 * sentinels to be intercepted at basic block boundaries before dispatch.
 *
 * Native imports and overrides mutate the same canonical X86pCpu that the JIT
 * executes. There is no title-local register/flag/x87 model and therefore no
 * state conversion at a hand-back.
 */
#ifndef X2_X86_ENGINE_H
#define X2_X86_ENGINE_H

#include <stdint.h>

struct X86pCpu;

/*
 * Build the required runtime JIT. Returns 0, having written `reason`, when
 * this host cannot provide it. No selector or fallback exists.
 */
int x2_engine_init(char *reason, unsigned reason_len);

/* Whether the required runtime JIT is ready. */
int x2_engine_active(void);

/* The product executor's fixed name, for reports. Never null. */
const char *x2_engine_name(void);

/*
 * Execute the guest function at `addr` with the canonical CPU context,
 * when it returns.
 *
 * Returns 1 when the function ran to completion. Returns 0 when the required
 * runtime JIT is not ready. Anything else ABORTS with the guest
 * address, the instruction, and why: a call this cannot finish leaves the
 * guest stack in a state nothing downstream can reason about.
 */
int x2_engine_call(uint32_t addr, struct X86pCpu *C);

/*
 * The program's own entry point, named before it is entered.
 *
 * It is the one call that is not meant to return: the game leaves through
 * exit(), so the engine's "this call is not finishing" cap must not apply to
 * it. Calls made FROM it are capped normally.
 */
void x2_engine_program_entry(uint32_t addr);

/*
 * What the engine did. Printed at shutdown beside the other run reports.
 *
 * Both halves of every ratio are published: calls that entered the engine
 * against instructions it executed, and the host call-outs it handed back to
 * the dispatcher. A run that entered the engine zero times and one that
 * interpreted a million instructions must not read the same.
 */
/*
 * Execute a program of the engine's own and check the result, before any guest
 * code runs. Returns 1 on success, 0 having said what failed.
 *
 * Called after the modules are mapped and the overrides resolved, because half
 * of what it checks is the predicate that decides when the engine must hand an
 * address back -- and that predicate has nothing to say before there is
 * anything to hand back to.
 */
int x2_engine_selftest(void);

/*
 * Where the engine is, RIGHT NOW. Printed on every stop path, because a host
 * backtrace stops at x2_engine_call and names no guest function below it.
 * Silent before initialization; says when the ready JIT has no guest call on
 * its stack, which is a different fact from "the engine is not here".
 */
void x2_engine_where(void);

void x2_engine_report(void);

#endif
