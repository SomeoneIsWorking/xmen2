/*
 * x86_engine.h -- running guest code the recompiler never translated.
 *
 * THE SEAM. `x86_native_call_at` answers "is there a body at this address",
 * and `x86_dispatch_one` currently aborts when the answer is no. That miss is
 * already a named, reached condition (jit-common I004, entry condition 2), and
 * it is exactly where a runtime engine plugs in: the statically recompiled
 * corpus keeps running, and this takes only what it could not translate.
 *
 * WHY THIS IS NOT "THE JIT IS ON NOW". The engine selected here is the
 * INTERPRETER, x86port's semantics authority. The JIT backend translates whole
 * basic blocks including their direct CALLs, and a block that calls a
 * statically recompiled body would jump into HOST code with a guest EIP. That
 * needs a call-out predicate x86port does not have yet, so selecting `jit`
 * through this consumer is REFUSED by name rather than quietly downgraded --
 * "I selected the JIT" and "the JIT never ran" must not be the same run.
 *
 * THE BOUNDARY THIS IS ONLY CORRECT AT. x2_engine_call bridges two different
 * machine models, and it is exact only at a Win32 FUNCTION CALL boundary:
 *
 *   - The substrate's CPU has no AF and no DF. At a call boundary neither is
 *     live: the ABI requires DF clear on entry and exit, and nothing a C
 *     compiler emits reads AF across a call (only the BCD instructions use it,
 *     and they use it within one instruction).
 *   - The substrate's CPU has no EIP, because its bodies are C functions. The
 *     engine needs one, which is what the return trampoline below is for.
 *   - The x87 stack is empty at a call boundary, except for a float return
 *     value in ST(0). Both directions are checked rather than assumed.
 *
 * Every one of those is asserted at the boundary, not documented and hoped
 * for: a violated one is reported with the guest address that violated it.
 */
#ifndef X2_X86_ENGINE_H
#define X2_X86_ENGINE_H

#include <stdint.h>

struct CPU;

/*
 * Resolve the engine for this run and build it. Reads X2_ENGINE; the default
 * is the substrate, so a build that has this linked behaves exactly as before
 * until someone asks for something else.
 *
 * Returns 1 when the port may proceed. Returns 0, having written `reason`,
 * when a requested engine cannot be provided -- never a silent fallback.
 */
int x2_engine_init(char *reason, unsigned reason_len);

/* Whether an engine other than the substrate is selected. */
int x2_engine_active(void);

/* The selected engine's name, for reports. Never null. */
const char *x2_engine_name(void);

/*
 * Execute the guest function at `addr` with the substrate's CPU, returning
 * when it returns.
 *
 * Returns 1 when the function ran to completion. Returns 0 when no engine is
 * selected -- the caller's existing "no body here" report is then the right
 * answer and nothing has been touched. Anything else ABORTS with the guest
 * address, the instruction, and why: a call this cannot finish leaves the
 * guest stack in a state nothing downstream can reason about.
 */
int x2_engine_call(uint32_t addr, struct CPU *C);

/*
 * The same call, entered because X2_ENGINE_TAKE made the substrate DECLINE a
 * body it has -- see x86_engine_take.h. Counted apart from the miss path, so a
 * run can say which of the two reasons put guest code through the engine.
 */
int x2_engine_call_taken(uint32_t addr, struct CPU *C);

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

void x2_engine_report(void);

#endif
