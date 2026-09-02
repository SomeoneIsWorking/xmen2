/*
 * x86_engine_take.h -- making the substrate DECLINE, so the engine can be
 * measured on real game code.
 *
 * WHY THIS EXISTS. x86_engine.c sits on a MISS: it runs only what the static
 * corpus could not translate. On X-Men Legends II the corpus translated
 * everything the game reaches, so a 60-frame run entered the engine ZERO
 * times -- the seam works and has never executed a guest instruction of the
 * game. That is not a measurement of anything (jit-common I004, step 3).
 *
 * X2_ENGINE_TAKE names bodies the substrate must hand over even though it has
 * them. The same function then runs both ways across two runs, which is what
 * makes "the engine is correct on real game code" and "this is what it costs
 * per frame" answerable questions rather than assertions.
 *
 *   X2_ENGINE_TAKE=0x004a1230,0x004a5560   these entry points, mapped
 *   X2_ENGINE_TAKE=0x00400000-0x00500000   every body in a mapped range
 *   X2_ENGINE_TAKE=msdia80.dll             every body in a module
 *   X2_ENGINE_TAKE=@scratch/take.txt       one entry per line, # comments
 *   X2_ENGINE_TAKE=all                     every translated body reached
 *                                          through the dispatcher
 *
 * The range and module forms exist to make a divergence BISECTABLE. `all`
 * either survives or does not, and halving the set is the only way to turn
 * "it breaks somewhere" into an address.
 *
 * HOST CODE IS NEVER TAKEN, in any mode. An import thunk and a native override
 * are C functions with no guest bytes at their address: interpreting one would
 * decode host memory as x86-32. `all` therefore means every address that has
 * GUEST BYTES to run, and naming a thunk or an override explicitly is refused
 * rather than silently ignored -- a take set that quietly drops half its
 * entries would report a measurement of something else.
 *
 * A NAMED ADDRESS WITH NO BODY IS ALSO A REFUSAL, and so is a range or module
 * containing none. "Decline the body at X" where nothing is at X is a typo,
 * and honouring it as "take nothing" is the failure mode that makes a run look
 * like it measured something.
 */
#ifndef X2_X86_ENGINE_TAKE_H
#define X2_X86_ENGINE_TAKE_H

#include <stdint.h>

/*
 * Parse X2_ENGINE_TAKE. Called once, right after the engine is selected.
 *
 * `engine_selected` is whether an engine other than the substrate is active.
 * Asking the substrate to decline bodies for an engine that is not there is a
 * contradiction, not a no-op, so it is REFUSED: returns 0 with `reason`.
 * Returns 1 when the port may proceed.
 */
int x2_take_init(int engine_selected, char *reason, unsigned reason_len);

/*
 * WHO is asking, because the same answer means two different things.
 *
 * The dispatcher asks whether to ROUTE a call to the engine; the engine asks,
 * at every instruction, whether to keep interpreting into a body instead of
 * handing it back. Counting both as one number made a 794-call run report 1149
 * "dispatches", which is a denominator that cannot be checked against
 * anything.
 */
typedef enum {
    kX2TakeDispatch = 0, /* the dispatcher: route this call to the engine? */
    kX2TakeInline        /* the engine: interpret into this body rather than
                            hand it back? */
} X2TakeSite;

/*
 * Must the substrate hand `addr` to the engine? 0 for host code and for
 * anything not named, always -- including when nothing was requested, which is
 * the default and leaves dispatch exactly as it was.
 */
int x2_take_has(uint32_t addr, X2TakeSite site);

/*
 * Check every explicitly named address against the resolved tables, now that
 * the modules are mapped and the overrides are in. Returns 1 when every one of
 * them is a translated guest body; 0, having said which one is not and why.
 *
 * Cannot run at init: an address is only classifiable once there is something
 * to classify it against.
 */
int x2_take_validate(void);

/* What was asked for and what it caught. Printed beside the engine's own
   report, because "5 entry points requested" and "5 entered" are different
   facts and a take set that never fired must not read as one that did. */
void x2_take_report(void);

#endif
