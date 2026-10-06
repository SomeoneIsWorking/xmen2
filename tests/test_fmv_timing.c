#include "fmv_timing.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>

static int near(double left, double right) {
  return fabs(left - right) < 0.000001;
}

int main(void) {
  const int64_t no_timestamp = INT64_MIN;
  X2FmvTimeline timeline;
  double first, duplicate, fallback, clamped, later;
  int failures = 0;
  x2_fmv_timeline_init(&timeline, 30.0);
  first = x2_fmv_timestamp(&timeline, 0, no_timestamp, 1, 90000, 3000);
  duplicate = x2_fmv_timestamp(&timeline, 0, no_timestamp, 1, 90000, 3000);
  fallback =
      x2_fmv_timestamp(&timeline, no_timestamp, no_timestamp, 1, 90000, 3000);
  clamped = x2_fmv_timestamp(&timeline, 0, no_timestamp, 1, 90000, 3000);
  later = x2_fmv_timestamp(&timeline, 18000, no_timestamp, 1, 90000, 3000);
  failures += !near(first, 0.0);
  failures += !near(duplicate, 1.0 / 30.0);
  failures += !near(fallback, 2.0 / 30.0);
  failures += !near(clamped, 3.0 / 30.0);
  failures += !near(later, 0.2);
  failures += timeline.timestamp_fallbacks != 1;
  failures += timeline.timestamp_clamps != 2;
  printf("FMV timing: %s -- best-effort PTS, duration fallback, and "
         "non-monotonic clamp share one production timeline\n",
         failures ? "FAILED" : "PASSED");
  return failures != 0;
}
