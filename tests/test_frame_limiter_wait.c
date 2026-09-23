/*
 * The frame limiter's sleep length (src/native/frame_limiter_wait.c): the
 * sleep must end before the frame's minimum time does, so the retail loop's
 * own clock read still decides when the frame ends.
 */
#include "frame_limiter_wait.h"

#include <math.h>
#include <stdio.h>

static int g_failures;

static void expect(const char *what, float min_frame, float start, float last,
                   uint32_t want) {
  uint32_t got = frame_limiter_sleep_ms(min_frame, start, last);
  if (got != want) {
    printf("FAIL %s: got %u ms, want %u ms\n", what, got, want);
    g_failures++;
  }
}

int main(void) {
  const float sixtieth = 1.0f / 60.0f;
  /* 16.67 ms frame, 4 ms in: 12.67 left, less the 1 ms spin margin. */
  expect("most of a frame left", sixtieth, 100.0f, 100.004f, 11u);
  /* 16.67 ms frame, 15 ms in: 1.67 left, 0.67 after the margin. */
  expect("less than the margin left", sixtieth, 100.0f, 100.015f, 0u);
  expect("the frame is already over", sixtieth, 100.0f, 100.020f, 0u);
  /* X2_UNPACED zeroes the minimum. */
  expect("no minimum frame time", 0.0f, 100.0f, 100.0f, 0u);
  /* A read from before the frame began is at most a whole frame. */
  expect("a clock read before the frame began", 1.0f / 30.0f, 100.0f, 50.0f,
         32u);
  expect("a NaN minimum", NAN, 100.0f, 100.004f, 0u);
  expect("a NaN clock read", sixtieth, 100.0f, NAN, 0u);
  if (g_failures) {
    printf("%d failure(s)\n", g_failures);
    return 1;
  }
  printf("frame_limiter_wait: 7 checks passed\n");
  return 0;
}
