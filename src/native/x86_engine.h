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
#pragma once

#include <cstdint>

struct X86pCpu;

namespace x2::native {

/*
 * Build the required runtime JIT. Returns 0, having written `reason`, when
 * this host cannot provide it. No selector or fallback exists.
 */
int engine_init(char *reason, unsigned reason_len);

/* Whether the required runtime JIT is ready. */
int engine_active(void);

/* Mapping owner calls before replacing or revoking guest backing. */
void engine_invalidate_memory(uint32_t address, uint32_t size);

/* The product executor's fixed name, for reports. Never null. */
const char *engine_name(void);

/*
 * Execute the guest function at `addr` with the canonical CPU context,
 * when it returns.
 *
 * Returns 1 when the function ran to completion. Returns 0 when the required
 * runtime JIT is not ready. Anything else ABORTS with the guest
 * address, the instruction, and why: a call this cannot finish leaves the
 * guest stack in a state nothing downstream can reason about.
 */
int engine_call(uint32_t addr, struct X86pCpu *C);

/*
 * Continue a guest function at `addr`, an address INSIDE its body, until it
 * returns: the function's own return address is the word at `frame_esp`, and
 * the caller has already built the frame the body expects at `addr`. The same
 * contract as engine_call otherwise, which is this with `addr` the entry
 * and `frame_esp` the entry ESP.
 */
int engine_resume(uint32_t addr, struct X86pCpu *C, uint32_t frame_esp);

/* Release a finished guest pthread's worker-local WASM translations. */
void engine_detach_thread(void);

/*
 * The program's own entry point, named before it is entered.
 *
 * It is the one call that is not meant to return: the game leaves through
 * exit(), so the engine's "this call is not finishing" cap must not apply to
 * it. Calls made FROM it are capped normally.
 */
void engine_program_entry(uint32_t addr);

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
int engine_selftest(void);

/*
 * Where the engine is, RIGHT NOW. Printed on every stop path, because a host
 * backtrace stops at engine_call and names no guest function below it.
 * Silent before initialization; says when the ready JIT has no guest call on
 * its stack, which is a different fact from "the engine is not here".
 */
void engine_where(void);

void engine_report(void);

} // namespace x2::native
