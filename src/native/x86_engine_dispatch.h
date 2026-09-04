/*
 * x86_engine_dispatch.h -- servicing a hand-back point: run the host thunk or
 * native override body, then decide whether the JIT run can carry on in place
 * or must unwind to the title-owned call loop.
 *
 * Split from x86_engine_intercept.{c,h} because servicing pulls in the whole
 * engine (x86_dispatch and the thunk table) while the
 * predicates next door must stay linkable on their own for the intercept
 * decision test. One file per responsibility: predicates decide, this file
 * acts.
 */
#ifndef X2_X86_ENGINE_DISPATCH_H
#define X2_X86_ENGINE_DISPATCH_H

#include <stdint.h>

#include "jit_engine.h"

struct X86pCpu;

/*
 * Run the host code at `cpu->eip` -- an import or a resolved native override
 * body -- against the canonical CPU, then set `cpu->eip` to the guest return
 * address. Shared by the title call loop and the JIT dispatch handler.
 */
void x86_engine_run_host_at(struct X86pCpu *cpu);

/*
 * x86port's between-blocks dispatch hook (x86p_jit_engine_set_dispatch): the
 * intercept predicate already fired; either service the hand-back and return
 * kX86pDispatchContinue so the run stays inside x86p_jit_engine_run, or return
 * kX86pDispatchUnwind to return to the title call loop for cases that need its
 * live host frame (a return to the translated call's caller, a
 * setjmp3 thunk, the engine return trampoline).
 */
X86pJitDispatchResult x86_engine_jit_dispatch(struct X86pCpu *cpu, void *user);

#endif /* X2_X86_ENGINE_DISPATCH_H */
