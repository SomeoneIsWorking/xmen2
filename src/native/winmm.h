/*
 * WINMM: the multimedia timers, run on the guest's own thread.
 *
 * See winmm.c for why they are deferred rather than threaded, and what that
 * costs in resolution.
 */
#ifndef X2_WINMM_H
#define X2_WINMM_H

/*
 * Run any timer callback that is due. Called from places the guest reaches
 * anyway -- a clock read, a sleep -- because a timer nobody pumps never fires,
 * and this host has no thread to fire it from.
 */
void winmm_timers_pump(void);

void winmm_report(void);

#endif /* X2_WINMM_H */
