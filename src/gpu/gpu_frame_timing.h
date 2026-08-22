#ifndef X2_GPU_FRAME_TIMING_H
#define X2_GPU_FRAME_TIMING_H

#define GPU_FRAME_HISTOGRAM_BUCKETS 13

/* Dense below 100 ms so normal pacing and isolated stalls do not collapse
   into the same average; the final bucket is open-ended. */
int gpu_frame_timing_bucket(unsigned long long nanoseconds);

#endif
