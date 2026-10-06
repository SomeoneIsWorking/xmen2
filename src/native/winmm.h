/*
 * WINMM: the multimedia timers, run on the guest's own thread.
 *
 * See winmm.c for why they are deferred rather than threaded, and what that
 * costs in resolution.
 */
#ifndef X2_WINMM_H
#define X2_WINMM_H

#include <stdint.h>

/*
 * Run any timer callback that is due. Called from places the guest reaches
 * anyway -- a clock read, a sleep -- because a timer nobody pumps never fires,
 * and this host has no thread to fire it from.
 */
void winmm_timers_pump(void);

/* The same pump at an instant the caller has already read from the guest
   clock, so a caller that has just asked the time does not ask twice. */
void winmm_timers_pump_at(double now_s);

/*
 * Milliseconds until the earliest timer is due, capped at `cap`; 0 if one is
 * due now, `cap` if there are no timers. A blocking wait uses this as its
 * timeout, because the waiting thread is the one that will fire the callback
 * that ends the wait -- see winmm.c.
 */
uint32_t winmm_next_due_ms(uint32_t cap);

void winmm_counts(unsigned long *fires, unsigned long *pumps, int *live);

void winmm_report(void);

#endif /* X2_WINMM_H */
