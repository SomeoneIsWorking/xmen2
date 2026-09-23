/*
 * One clock, and the guest sees all of it.
 *
 * guest_clock.h says QueryPerformanceCounter and GetTickCount come from this
 * owner. They did not: both KERNEL32 entry points and the JIT's import fast
 * path each read CLOCK_MONOTONIC themselves, so guest_clock_ns had no callers
 * at all and the idle skew the owner exists to apply reached none of them.
 * A run with --unbounded therefore skipped real seconds that the guest's own
 * QueryPerformanceCounter never saw, which is a timing bug presenting as a
 * gameplay bug -- exactly what the header was written to prevent.
 *
 * These checks are about the OWNER, at its own interface. The import wrappers
 * need a CPU and a mapped guest to call, which is a different fixture; what
 * they can be held to is that the value they now return is this one.
 *
 * The discriminator is the idle skip, because that is the only thing a
 * private CLOCK_MONOTONIC reading gets WRONG: it is monotonic, it agrees with
 * the seconds view to well inside a millisecond, and it has nanosecond
 * resolution either way. Only the skew tells the two apart. An earlier
 * version of this file also checked that the counter was computed in integers
 * rather than by scaling a double, on the theory that the scaling lost the low
 * bits; it does not -- the double holds seconds since boot, not nanoseconds --
 * and those checks passed against both forms. A check that cannot fail is
 * worse than no check, so they are gone.
 */
#include "guest_clock.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

static unsigned checks, failures;

static void check(int value, const char *message) {
  ++checks;
  if (!value) {
    ++failures;
    printf("FAIL: %s\n", message);
  }
}

int main(void) {
  uint64_t before, after;
  double seconds_before, seconds_after;

  /* Monotonic, and in step with the seconds view of the same clock. */
  before = guest_clock_ns();
  seconds_before = guest_clock_now_s();
  after = guest_clock_ns();
  seconds_after = guest_clock_now_s();
  check(after >= before, "the nanosecond counter went backwards");
  check(seconds_after >= seconds_before, "the seconds counter went backwards");
  check((double)before / 1e9 >= seconds_before - 0.01 &&
            (double)after / 1e9 <= seconds_after + 0.01,
        "the nanosecond and seconds views of the same clock disagree by more "
        "than 10 ms");

  /* The coarse view is the same clock at tick resolution: never ahead of a
     precise reading taken after it, and never a tick-sized step behind one
     taken before it. A different clock -- REALTIME counts from 1970, not from
     boot -- is off by decades either way. */
  {
    const double fine_before = guest_clock_now_s();
    const double coarse = guest_clock_coarse_now_s();
    const double fine_after = guest_clock_now_s();
    check(coarse <= fine_after,
          "the coarse clock ran ahead of the precise one");
    check(coarse > fine_before - 0.1,
          "the coarse clock lags the precise one by more than 100 ms");
    printf("  coarse reading %.6f s behind the precise one\n",
           fine_after - coarse);
  }

  /*
   * The idle skip, which is the whole reason a private clock reading is a
   * bug. With unbounded mode on, skipping to a deadline must move the
   * nanosecond counter too -- a counter that ignored the skew would be
   * unchanged here, which is precisely what the guest was being handed.
   */
  guest_clock_set_unbounded(1);
  before = guest_clock_ns();
  check(guest_clock_skip_idle_to(guest_clock_now_s() + 5.0),
        "skipping five seconds forward was refused");
  after = guest_clock_ns();
  check(after > before + 4000000000ULL,
        "the nanosecond counter did not follow the idle skip, so a private "
        "clock reading would disagree with it");
  check(after < before + 6000000000ULL, "the idle skip moved the nanosecond "
                                        "counter further than it was asked to");
  check(guest_clock_coarse_now_s() > (double)before / 1e9 + 4.0,
        "the coarse clock did not follow the idle skip");
  printf("  the five-second skip moved the counter by %" PRIu64 " ns\n",
         after - before);

  /* And with unbounded off, the same request must change nothing, so the
     check above is measuring the skew and not the passage of real time. */
  guest_clock_set_unbounded(0);
  before = guest_clock_ns();
  check(!guest_clock_skip_idle_to(guest_clock_now_s() + 5.0),
        "a bounded run must refuse to skip");
  after = guest_clock_ns();
  check(after < before + 1000000000ULL,
        "a refused skip moved the clock anyway");

  printf("guest clock: %u checks, %u failures\n", checks, failures);
  return failures ? 1 : 0;
}
