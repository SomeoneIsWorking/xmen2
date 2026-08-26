#include "gpu_frame_timing.h"

/* Present-to-present frame timing: the accumulator behind gpu_device_perf.
   The state lives here rather than in gpu_device.c so the frame-end path
   composes the measurement instead of growing the device file around it. */

static unsigned long long g_frame_ns, g_frame_ns_min, g_frame_ns_max;
static unsigned long long g_last_frame_end_ns;
static unsigned long g_frame_intervals;  /* intervals folded into g_frame_ns */
static unsigned long g_hist[GPU_FRAME_HISTOGRAM_BUCKETS];

/* Dense below 100 ms so normal pacing and isolated stalls do not collapse
   into the same average; the final bucket is open-ended. */
static int bucket_of(unsigned long long nanoseconds)
{
    static const unsigned long long EDGE_MS[] = {
        1, 2, 4, 6, 10, 16, 25, 40, 60, 80, 120, 200,
    };
    unsigned long long milliseconds = nanoseconds / 1000000ull;
    unsigned i;
    for (i = 0; i < GPU_FRAME_HISTOGRAM_BUCKETS - 1; i++)
        if (milliseconds < EDGE_MS[i]) return (int)i;
    return GPU_FRAME_HISTOGRAM_BUCKETS - 1;
}

/* A frame slow enough to matter gets its cost attributed AT THE MOMENT it
   ends, not in a shutdown report: the frame limiter makes "slow" a
   wall-clock number and this is the only place the wall clock and the host
   share of the same frame meet.

   The threshold is an ORDER OF MAGNITUDE above the paced frame budget
   (60fps = 16.7ms), so a normal gameplay frame never prints and a load
   stall does -- exactly the frames that want asking "who was slow?". Said
   once per frame, since a load can stall for several. */
#define SLOW_FRAME_NS 160000000ull

void (*gpu_frame_timing_slow_hook)(unsigned long frame,
                                   unsigned long long dt_ns);

void gpu_frame_timing_note(unsigned long long now_ns,
                           unsigned long frame)
{
    unsigned long long dt;

    if (g_last_frame_end_ns) {
        dt = now_ns - g_last_frame_end_ns;
        g_frame_ns += dt;
        g_frame_intervals++;
        if (!g_frame_ns_min || dt < g_frame_ns_min) g_frame_ns_min = dt;
        if (dt > g_frame_ns_max) g_frame_ns_max = dt;
        g_hist[bucket_of(dt)]++;
        if (dt > SLOW_FRAME_NS) {
            if (gpu_frame_timing_slow_hook)
                gpu_frame_timing_slow_hook(frame, dt);
        }
    }
    g_last_frame_end_ns = now_ns;
}

void gpu_frame_timing_perf(unsigned long long *frame_ns,
                           unsigned long long *frame_ns_min,
                           unsigned long long *frame_ns_max,
                           unsigned long *intervals,
                           const unsigned long **hist)
{
    *frame_ns = g_frame_ns;
    *frame_ns_min = g_frame_ns_min;
    *frame_ns_max = g_frame_ns_max;
    *intervals = g_frame_intervals;
    *hist = g_hist;
}
