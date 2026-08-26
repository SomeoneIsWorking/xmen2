#ifndef X2_GPU_FRAME_TIMING_H
#define X2_GPU_FRAME_TIMING_H

#define GPU_FRAME_HISTOGRAM_BUCKETS 13

/* Fold one present-to-present interval in. The FIRST frame has no
   predecessor; it seeds the baseline rather than measuring an
   intervals-prior frame. `frame` (the presented count) and the interval
   itself ride along only for the slow-frame report. The hook is the
   device's slow-frame printer -- a function pointer so this file does not
   need the host-share internals. */
extern void (*gpu_frame_timing_slow_hook)(unsigned long frame,
                                          unsigned long long dt_ns);
void gpu_frame_timing_note(unsigned long long now_ns, unsigned long frame);

/* The heartbeat's view of the same numbers. */
void gpu_frame_timing_perf(unsigned long long *frame_ns,
                           unsigned long long *frame_ns_min,
                           unsigned long long *frame_ns_max,
                           unsigned long *intervals,
                           const unsigned long **hist);

#endif
