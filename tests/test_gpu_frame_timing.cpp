#include "gpu_frame_timing.h"

#include <stdio.h>

int main(void) {
  unsigned long long p50, p95, p99;
  unsigned long long now = 1;
  unsigned long samples;
  unsigned i;

  gpu_frame_timing_note(1, 0);
  for (i = 1; i <= 100; ++i) {
    now += (unsigned long long)i * 1000000ull;
    gpu_frame_timing_note(now, i);
  }
  gpu_frame_timing_percentiles(&p50, &p95, &p99, &samples);
  if (samples != 100 || p50 != 50000000ull || p95 != 95000000ull ||
      p99 != 99000000ull) {
    fprintf(stderr, "frame timing: samples=%lu p50=%llu p95=%llu p99=%llu\n",
            samples, p50, p95, p99);
    return 1;
  }

  for (i = 101; i <= GPU_FRAME_TIMING_SAMPLE_CAPACITY + 100u; ++i) {
    now += (unsigned long long)i * 1000000ull;
    gpu_frame_timing_note(now, i);
  }
  gpu_frame_timing_percentiles(&p50, &p95, &p99, &samples);
  if (samples != GPU_FRAME_TIMING_SAMPLE_CAPACITY ||
      p50 != (100ull + (GPU_FRAME_TIMING_SAMPLE_CAPACITY * 50u + 99u) / 100u) *
                 1000000ull ||
      p95 != (100ull + (GPU_FRAME_TIMING_SAMPLE_CAPACITY * 95u + 99u) / 100u) *
                 1000000ull ||
      p99 != (100ull + (GPU_FRAME_TIMING_SAMPLE_CAPACITY * 99u + 99u) / 100u) *
                 1000000ull) {
    fprintf(stderr, "frame timing: bounded sample window failed\n");
    return 1;
  }
  /* The window covers only what was noted since it was last read, across
     the ring's wrap: every interval so far is one window, then ten fresh
     1 ms intervals are the next, whatever the whole-run tail says. */
  gpu_frame_timing_window_percentiles(&p50, &p95, &p99, &samples);
  if (samples != GPU_FRAME_TIMING_SAMPLE_CAPACITY) {
    fprintf(stderr, "frame timing: first window held %lu samples\n", samples);
    return 1;
  }
  for (i = 0; i < 10u; ++i) {
    now += 1000000ull;
    gpu_frame_timing_note(now, i);
  }
  gpu_frame_timing_window_percentiles(&p50, &p95, &p99, &samples);
  if (samples != 10u || p50 != 1000000ull || p99 != 1000000ull) {
    fprintf(stderr, "frame timing: window samples=%lu p50=%llu p99=%llu\n",
            samples, p50, p99);
    return 1;
  }
  gpu_frame_timing_window_percentiles(&p50, &p95, &p99, &samples);
  if (samples || p50 || p99) {
    fprintf(stderr, "frame timing: an empty window reported %lu samples\n",
            samples);
    return 1;
  }
  gpu_frame_timing_reset();
  gpu_frame_timing_percentiles(&p50, &p95, &p99, &samples);
  if (samples || p50 || p95 || p99) {
    fprintf(stderr, "frame timing: reset retained stale samples\n");
    return 1;
  }
  puts("gpu frame timing: exact p50/p95/p99 passed");
  return 0;
}
