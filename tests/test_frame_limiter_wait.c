/*
 * The frame limiter's sleep length (src/native/frame_limiter_wait.c): the
 * sleep must end before the frame's minimum time does, so the retail loop's
 * own clock read still decides when the frame ends.
 */
#include "frame_limiter_wait.h"

#include <math.h>
#include <stdio.h>

static int g_failures;

/* Within 3 us: the clock reads are floats, and 100.004f is not 100.004. */
static void expect(const char *what, float min_frame, float start, float last,
                   uint32_t want) {
  uint32_t got = frame_limiter_sleep_us(min_frame, start, last);
  if (got + 3u < want || got > want + 3u) {
    printf("FAIL %s: got %u us, want %u us\n", what, got, want);
    g_failures++;
  }
}

int main(void) {
  const float sixtieth = 1.0f / 60.0f;
  /* 16.67 ms frame, 4 ms in: 12.67 ms left, less the 250 us spin margin. */
  expect("most of a frame left", sixtieth, 100.0f, 100.004f, 12419u);
  /* 15 ms in: 1.67 ms left. Whole milliseconds less a millisecond slept
     nothing here and spun all of it. */
  expect("under two milliseconds left", sixtieth, 100.0f, 100.015f, 1417u);
  /* 16.3 ms in: 0.37 ms left, 0.12 after the margin -- worth sleeping. */
  expect("just over the margin left", sixtieth, 100.0f, 100.0163f, 117u);
  /* 16.4 ms in: 0.27 ms left, 0.02 after the margin -- not worth a sleep. */
  expect("inside the margin", sixtieth, 100.0f, 100.0164f, 0u);
  expect("the frame is already over", sixtieth, 100.0f, 100.020f, 0u);
  /* X2_UNPACED zeroes the minimum. */
  expect("no minimum frame time", 0.0f, 100.0f, 100.0f, 0u);
  /* A read from before the frame began is at most a whole frame. */
  expect("a clock read before the frame began", 1.0f / 30.0f, 100.0f, 50.0f,
         33083u);
  expect("a NaN minimum", NAN, 100.0f, 100.004f, 0u);
  expect("a NaN clock read", sixtieth, 100.0f, NAN, 0u);
  if (g_failures) {
    printf("%d failure(s)\n", g_failures);
    return 1;
  }
  printf("frame_limiter_wait: 9 checks passed\n");
  return 0;
}
