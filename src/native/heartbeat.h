/*
 * A liveness heartbeat for a run that does not return.
 *
 * Most diagnostics here speak at the END of a run: the boundary ring and the
 * subsystem reports. That is fine for a run that crashes or exits, and useless
 * for one that keeps going -- which is what the game does the moment it reaches
 * its main loop. Issue #35 read
 * exactly like a hang for a whole session: the log stopped, and the only
 * evidence available afterwards was the ring, whose last entries happened to
 * be the frame timer. "Spinning on the clock" and "running frames" produce the
 * SAME silence.
 *
 * So this prints one line every few seconds, from a host thread that the guest
 * cannot starve, and it prints whether or not anything is happening. The
 * zero-delta cases are spelled out in words, because that is the whole point:
 *
 *   crossings unchanged  -> the guest executed nothing; it is blocked inside
 *                           host code, not looping
 *   presents unchanged   -> the loop runs but never reaches Present
 *   draws unchanged      -> frames are presented with nothing drawn in them
 *
 * X2_HEARTBEAT=<seconds>   period; 0 disables it. Default 5.
 */
#ifndef X2_HEARTBEAT_H
#define X2_HEARTBEAT_H

#include <signal.h> /* sig_atomic_t, for the interrupt flag below */

/* Starts the thread. Announces itself (or that it is disabled) on stderr, so a
   run with no [HB] lines cannot be mistaken for a run that produced none. */
void heartbeat_start(void);

/* Whether the thread is there to be handed the interrupt reports. */
int heartbeat_running(void);

/* Set by the signal handler; the thread prints the reports and exits. */
/* 0 idle, 1 a signal asked for the reports, 2 the frame limit did. The
   difference decides whether the boundary ring is dumped -- see
   x2_interrupt_reports. */
extern volatile sig_atomic_t x2_report_now;

/* Everything a normal exit would print, callable from ordinary context.
   Defined in x2native.c, which is what knows the full list. */
void x2_interrupt_reports(int killed);

#endif /* X2_HEARTBEAT_H */
