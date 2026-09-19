#ifndef X2_HEARTBEAT_REPORTS_H
#define X2_HEARTBEAT_REPORTS_H

/*
 * The subsystems that account for themselves on every beat.
 *
 * This is the periodic twin of run_reports.h: that one is the roll-call at
 * an ending, this one is the roll-call on a schedule. They are separate
 * because a product that never exits -- the browser tab -- reaches only this
 * one, and because the beat loop itself should measure its own deltas rather
 * than also knowing about the clock, the controls and the pad.
 *
 * Every call here prints on every beat, its zero case included. A report
 * that goes quiet when it has nothing to say cannot be told apart from one
 * nobody wired up.
 */
void heartbeat_subsystem_reports(void);

/* The multimedia-timer line on the beat; prints nothing only when no timer
   has ever existed, so silence is never a missing counter. */
void heartbeat_winmm_report(void);

/* The blocking waits, the lock hand-offs, and which guest code called Sleep. */
void heartbeat_wait_report(void);

#endif /* X2_HEARTBEAT_REPORTS_H */
