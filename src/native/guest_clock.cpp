#include "guest_clock.h"
#include <lucent/log_c.h>

#include <stdatomic.h>
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

/* The most recent precise reading any thread took; 0 before the first. */
static _Atomic uint64_t g_last_real_ns;

static uint64_t real_now_ns(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  const uint64_t ns =
      (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
  atomic_store_explicit(&g_last_real_ns, ns, memory_order_relaxed);
  return ns;
}

static double real_now_s(void) { return (double)real_now_ns() / 1e9; }

/*
 * The last precise reading, not a new one. Every host has one to give: the
 * guest asks the time many times a frame and the mixer every few tens of
 * milliseconds, and each of those readings lands here. Reading the clock
 * instead cost a call out of WebAssembly into performance.now() at every host
 * crossing -- 75% of the browser's clock samples, ~6% of its guest worker --
 * because Emscripten has no CLOCK_MONOTONIC_COARSE to fall back on.
 *
 * Threads store their readings in no particular order, so this can step back
 * by the width of a race; it can never be ahead of a reading taken after it.
 */
static uint64_t real_coarse_ns(void) {
  const uint64_t last =
      atomic_load_explicit(&g_last_real_ns, memory_order_relaxed);
  return last ? last : real_now_ns();
}

double guest_clock_now_s(void) {
  if (g_start_real == 0.0)
    g_start_real = real_now_s();
  return real_now_s() + g_skew;
}

double guest_clock_coarse_now_s(void) {
  return (double)real_coarse_ns() / 1e9 + g_skew;
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

/*
 * Nanoseconds, from the same clock and the same skew as the seconds view.
 *
 * Computed in integers rather than by scaling guest_clock_now_s, which is one
 * multiply and one divide less per call on a path the guest hits constantly.
 * Precision is NOT the reason: the double holds seconds since boot, around
 * 1e5, where a 53-bit significand resolves far below a nanosecond. That was
 * this comment's first claim and it was wrong -- the test written to prove it
 * passed against both forms, which is how it was caught.
 */
uint64_t guest_clock_ns(void) {
  const double skew = g_skew;
  const uint64_t real = real_now_ns();
  /* The skew only ever moves forward (see guest_clock_skip_idle_to), so this
     addition cannot go backwards; the guard is for the state, not the sum. */
  return skew > 0.0 ? real + (uint64_t)(skew * 1e9) : real;
}

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
