/* The reached set -- which recompiled bodies a run has actually entered.
 *
 * "Does the engine ever call this function?" used to cost a throwaway native
 * override, a module re-emit and a 272MB relink per yes/no question, so the
 * question got answered by reading code and guessing. This instrument answers
 * it from a live run: every generated body's entry macro reports in, the set
 * answers per-address verdicts with first-entry ORDER and call COUNT, and the
 * report prints its denominators so a NEVER from an unarmed run cannot read
 * as a negative.
 *
 * This file owns the instrument AND its two doors: the environment arming at
 * launch, and the /reached HTTP endpoint on the control channel. The hot-path
 * entry hook stays declared in x86rt.h beside the entry macro that calls it.
 */
#ifndef X2_X86_REACHED_H
#define X2_X86_REACHED_H

#include <stdint.h>

/* Arm from the X2_REACHED / X2_REACHED_SELFTEST environment; asking implies
 * arming, because a run that collects nothing must not report NEVERs. */
void x86_reached_arm_from_env(void);

/* The control-channel endpoint: /reached?arm=1, /reached?ep=0x...
 * [&module=...]. Replies on fd itself; the HTTP helpers are the control
 * framework's. */
void x86_reached_route(int fd, const char *query);

/* One line at shutdown, at zero and with its denominator. Safe to call on
 * every ending; says so when it was never armed rather than staying silent. */
void x86_reached_report(void);

#endif /* X2_X86_REACHED_H */
