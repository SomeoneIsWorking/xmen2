/*
 * x86_dispatch.c -- which engine runs the body at a guest address.
 *
 * ONE OWNER, THREE ANSWERS. Every indirect guest call in the recompiled corpus
 * arrives here with a mapped address and nothing else, and exactly one of the
 * following happens:
 *
 *   1. the TAKE set names it, so the substrate is made to decline a body it
 *      HAS and the engine runs the guest bytes instead;
 *   2. the substrate has a body, and runs it;
 *   3. neither -- which is the MISS the engine was wired onto, because the
 *      corpus could not translate that address;
 *
 * and if none of them does, the run stops. There is no fourth branch that
 * returns a plausible value: see the abort-paths note in x86rt_native.c.
 *
 * Split out of x86rt_native.c when (1) was added. The dispatch loop is a
 * policy about engines, and the file it came from is the module/thunk/override
 * table -- two different things that had grown into one.
 */
#include "x86rt.h"
#include "x86rt_native.h"
#include "x86_dispatch_report.h"
#include "x86_engine.h"
#include "x86_engine_take.h"
#include "x86_tail_policy.h"

#include <stdlib.h>

static void x86_dispatch_one(CPU *C, uint32_t target)
{
    /*
     * The TAKE set is asked FIRST, and it is the only thing that can make the
     * substrate give up a body it has.
     *
     * Without it the engine runs only what the corpus could not translate, and
     * on this title the corpus translated everything the game reaches -- so a
     * 60-frame run entered the engine zero times and measured nothing about
     * the game. X2_ENGINE_TAKE names entry points to hand over anyway, which
     * is what lets the same function be run both ways and compared
     * (jit-common I004 step 3).
     *
     * It answers 0 for host code and 0 when nothing was requested, which is
     * the default -- so this line is inert on an ordinary run rather than
     * being a branch that has to be reasoned about.
     */
    if (x2_take_has(target) && x2_engine_call_taken(target, C)) return;
    if (x86_native_call_at(target, C)) return;
    /*
     * No body here -- which is exactly where a runtime engine plugs in
     * (jit-common I004). It returns 0 when no engine is selected, and then the
     * report below is still the right answer; nothing has been touched.
     *
     * It is asked AFTER x86_native_call_at rather than instead of it, so the
     * statically recompiled corpus keeps running and the engine takes only
     * what that corpus could not translate. That is what makes this
     * incremental rather than a switch-over.
     */
    if (x2_engine_call(target, C)) return;
    x86_report_missing_body(C, target);
    abort();
}

void x86_dispatch(CPU *C, uint32_t target)
{
    uint32_t outer_depth = C->dispatch_depth;
    uint32_t outer_target = C->tail_target;
    C->dispatch_depth = C->call_depth + 1u;
    do {
        C->tail_target = 0;
        x86_dispatch_one(C, target);
        target = C->tail_target;
    } while (target);
    C->dispatch_depth = outer_depth;
    C->tail_target = outer_target;
}

void x86_tail_dispatch(CPU *C, uint32_t target)
{
    /*
     * Same generated-body contract as the hosted runtime (C181). A tail jump
     * reached through a direct C call must finish before that direct caller
     * resumes; only a tail at THIS dispatch frame may be queued for the loop.
     *
     * The one-level depth relation is the whole test. X86_TAIL_FN has already
     * decremented call_depth by the time this runs -- the body is leaving --
     * so the body that IS the dispatch frame arrives at dispatch_depth - 1,
     * and a body one direct call deeper arrives at exactly dispatch_depth.
     * Comparing the depths for equality therefore queued precisely the case
     * that must run inline: exactly backwards.
     *
     * What that cost: FUN_0046b750 -> FUN_00427c30 -> FUN_00426330, then a
     * tail jump to __security_check_cookie. The cookie check was queued rather
     * than run, so the return address it should have popped stayed on the
     * stack, FUN_0046b750 resumed four bytes low, and its own /GS epilogue
     * read the word below its cookie and reported a stack buffer overrun that
     * had never happened (issue #81, C213).
     */
    if (x86_tail_route(C->dispatch_depth, C->call_depth) == X86_TAIL_QUEUE) {
        C->tail_target = target;
        return;
    }
    x86_dispatch(C, target);
}
