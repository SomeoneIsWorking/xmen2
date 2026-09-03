/*
 * THE guest's clock -- one source of time for everything the guest can see.
 *
 * There were five private copies of `now_s()` reading CLOCK_MONOTONIC
 * directly, and that is not a tidiness complaint: the guest drives real logic
 * off elapsed time (multimedia timer deadlines, thread waits, DirectSound play
 * cursors, QueryPerformanceCounter), so two of those advancing differently is
 * a timing bug that presents as a gameplay bug. They now all come from here.
 *
 * The clock is REAL time plus a skew this file owns. Nothing scales it: a
 * scaled clock makes the guest experience time it did not spend, and every
 * rate the guest computes from it comes out wrong. The skew only ever jumps
 * FORWARD, and only over an interval in which, by construction, no guest
 * thread could run -- see guest_clock_skip_idle_to.
 *
 * Host-side instruments (the heartbeat's period, the sampling profiler) do NOT
 * use this. They measure how long the run really took, and a report that
 * skipped time along with the guest could not say that.
 */
#ifndef X2_GUEST_CLOCK_H
#define X2_GUEST_CLOCK_H

#include <stdint.h>

/* Seconds, monotonic, as the guest sees them. */
double guest_clock_now_s(void);

/* Seconds since the process started -- what to REPORT. guest_clock_now_s
   counts from the machine's boot, which the guest does not care about and a
   human reading a status line very much does. */
double guest_clock_elapsed_s(void);

/* The same instant in nanoseconds, for QueryPerformanceCounter (which this
   port defines as a nanosecond counter) and GetTickCount. */
uint64_t guest_clock_ns(void);

/*
 * Idle skip -- what "unbounded speed" actually is.
 *
 * Called by the scheduler at the one moment it knows NOTHING is runnable and
 * the earliest event anyone is waiting for is at `deadline` (guest seconds).
 * The wall time between now and then is time in which the program is defined
 * to do nothing. Skipping it removes real seconds without removing any guest
 * work: the same code runs, in the same order, seeing the same timestamps.
 *
 * Returns 1 if the clock was jumped (the caller must NOT sleep), 0 if the
 * caller should sleep for real -- which is the answer whenever unbounded mode
 * is off, and the reason the default run is still wall-clock paced.
 */
int guest_clock_skip_idle_to(double deadline);

/* Enabled by --unbounded or X2_UNBOUNDED=1. */
int guest_clock_unbounded(void);
void guest_clock_set_unbounded(int on);

/* Into the heartbeat and the shutdown report, at zero and with denominators. */
void guest_clock_report(void);

#endif
