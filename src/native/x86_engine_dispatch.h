/*
 * x86_engine_dispatch.h -- servicing a hand-back point: run the host thunk or
 * native override body, then decide whether the JIT run can carry on in place
 * or must unwind to the interpreter loop.
 *
 * Split from x86_engine_intercept.{c,h} because servicing pulls in the whole
 * engine (the callout bridge, x86_dispatch, the thunk table) while the
 * predicates next door must stay linkable on their own for the intercept
 * decision test. One file per responsibility: predicates decide, this file
 * acts.
 */
#ifndef X2_X86_ENGINE_DISPATCH_H
#define X2_X86_ENGINE_DISPATCH_H

#include <stdint.h>

#include "jit_engine.h"

struct CPU;
struct X86pCpu;

/*
 * The interpreter loop's live call context, threaded to the JIT dispatch
 * handler through a per-thread stack so the handler knows which interpreted
 * call it is nested inside (its entry point, where it returns to, its entry
 * ESP, and the host CPU to service a body against).
 */
typedef struct EngineCallCtx {
  struct EngineCallCtx *prev;
  struct CPU *host;
  uint32_t entry;
  uint32_t return_to;
  uint32_t entry_esp;
} EngineCallCtx;

void x86_engine_call_ctx_push(EngineCallCtx *slot, struct CPU *host,
                              uint32_t entry, uint32_t return_to,
                              uint32_t entry_esp);
void x86_engine_call_ctx_pop(void);
void x86_engine_call_ctx_restore(EngineCallCtx *slot);

/*
 * Run the host code at `cpu->eip` -- an import thunk or a resolved native
 * override body -- against `host`, then set `cpu->eip` to the guest return
 * address. Shared by the interpreter loop and the JIT dispatch handler.
 */
void x86_engine_run_host_at(struct X86pCpu *cpu, struct CPU *host);

/*
 * x86port's between-blocks dispatch hook (x86p_jit_engine_set_dispatch): the
 * intercept predicate already fired; either service the hand-back and return
 * kX86pDispatchContinue so the run stays inside x86p_jit_engine_run, or return
 * kX86pDispatchUnwind to fall back to the interpreter loop for the cases that
 * need its host frame back (a return to the interpreted call's caller, a
 * setjmp3 thunk, the engine return trampoline).
 */
X86pJitDispatchResult x86_engine_jit_dispatch(struct X86pCpu *cpu, void *user);

#endif /* X2_X86_ENGINE_DISPATCH_H */
