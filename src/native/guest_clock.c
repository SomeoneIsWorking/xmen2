#include "guest_clock.h"
#include <lucent/log_c.h>

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <lucent/cvar_c.h>

static int g_unbounded = -1; /* -1 = not yet read from runtime configuration */
static double g_skew;        /* guest time - real time, seconds */

/* What the skip bought, and what it could not. Reported even at zero: a run
   that skipped nothing is a run that was never idle, which is a measurement
   about the run and not a missing instrument. */
static unsigned long g_skips, g_idle_calls, g_refused_backwards;
static double g_skipped_s, g_largest_skip;
static double g_start_real;

static double real_now_s(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

double guest_clock_now_s(void) {
  if (g_start_real == 0.0)
    g_start_real = real_now_s();
  return real_now_s() + g_skew;
}

/*
 * Seconds since this process started.
 *
 * guest_clock_now_s is CLOCK_MONOTONIC-based and therefore counts from the
 * MACHINE's boot, which is correct for the guest -- only differences matter to
 * it -- and useless to report. Shown as-is it read "guest 8276.57s" 25 seconds
 * into a run, which looks exactly like a clock that has run away.
 */
double guest_clock_elapsed_s(void) {
  double now = guest_clock_now_s(); /* also latches g_start_real */
  return now - g_start_real;
}

uint64_t guest_clock_ns(void) { return (uint64_t)(guest_clock_now_s() * 1e9); }

int guest_clock_unbounded(void) {
  if (g_unbounded < 0) {
    g_unbounded = lucent_cvar_flag("unbounded", 0) != 0;
  }
  return g_unbounded;
}

void guest_clock_set_unbounded(int on) { g_unbounded = on ? 1 : 0; }

int guest_clock_skip_idle_to(double deadline) {
  double now, dt;

  g_idle_calls++;
  if (!guest_clock_unbounded())
    return 0;

  now = guest_clock_now_s();
  dt = deadline - now;
  if (dt <= 0.0) {
    /* The deadline has already passed, so there is nothing to skip and
       the caller was going to return immediately anyway. Counted rather
       than ignored: if this is most of the calls, the run is not idle-
       bound and unbounded mode is not what is making it slow. */
    g_refused_backwards++;
    return 0;
  }

  g_skew += dt;
  g_skips++;
  g_skipped_s += dt;
  if (dt > g_largest_skip)
    g_largest_skip = dt;
  return 1;
}

void guest_clock_report(void) {
  double real = g_start_real ? real_now_s() - g_start_real : 0.0;

  if (!guest_clock_unbounded()) {
    lucent_log_error(
        "x2",
        "  clock: wall-clock paced (unbounded mode OFF). The scheduler "
        "went idle %lu time(s) and slept through each one for real. "
        "--unbounded (or X2_UNBOUNDED=1) skips those waits.\n",
        g_idle_calls);
    return;
  }

  lucent_log_error(
      "x2",
      "  clock: UNBOUNDED. %lu of %lu idle wait(s) skipped, %.1fs of guest "
      "time not spent (largest single skip %.3fs); %lu wait(s) had already "
      "expired so there was nothing to skip.\n"
      "         %.1fs real elapsed, %.1fs guest elapsed -- the difference IS "
      "the skipped idle, and no guest work was removed to get it: the skip "
      "only ever covers an interval in which no thread was runnable.\n",
      g_skips, g_idle_calls, g_skipped_s, g_largest_skip, g_refused_backwards,
      real, real + g_skew);
}
