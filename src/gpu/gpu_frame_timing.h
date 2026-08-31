#ifndef X2_GPU_FRAME_TIMING_H
#define X2_GPU_FRAME_TIMING_H

#define GPU_FRAME_HISTOGRAM_BUCKETS 13
/* Enough for a 20-minute qualification run at 120 fps, with headroom. */
#define GPU_FRAME_TIMING_SAMPLE_CAPACITY 262144

/* Fold one present-to-present interval in. The FIRST frame has no
   predecessor; it seeds the baseline rather than measuring an
   intervals-prior frame. `frame` (the presented count) and the interval
   itself ride along only for the slow-frame report. The hook is the
   device's slow-frame printer -- a function pointer so this file does not
   need the host-share internals. */
extern void (*gpu_frame_timing_slow_hook)(unsigned long frame,
                                          unsigned long long dt_ns);
void gpu_frame_timing_note(unsigned long long now_ns, unsigned long frame);
void gpu_frame_timing_reset(void);

/* The heartbeat's view of the same numbers. */
void gpu_frame_timing_perf(unsigned long long *frame_ns,
                           unsigned long long *frame_ns_min,
                           unsigned long long *frame_ns_max,
                           unsigned long *intervals,
                           const unsigned long **hist);

/* Exact percentiles over the most recent bounded presentation intervals.
 * This is deliberately separate from the all-run accumulator above: an Android
 * qualification session needs p50/p95/p99 for its sustained window, while a
 * run that lives for hours must not grow unbounded telemetry state. */
void gpu_frame_timing_percentiles(unsigned long long *p50_ns,
                                  unsigned long long *p95_ns,
                                  unsigned long long *p99_ns,
                                  unsigned long *samples);

#endif
