#include "gpu_frame_timing.h"

#include <stdlib.h>
#include <string.h>

/* Present-to-present frame timing: the accumulator behind gpu_device_perf.
   The state lives here rather than in gpu_device.c so the frame-end path
   composes the measurement instead of growing the device file around it. */

static unsigned long long g_frame_ns, g_frame_ns_min, g_frame_ns_max;
static unsigned long long g_last_frame_end_ns;
static unsigned long g_frame_intervals;  /* intervals folded into g_frame_ns */
static unsigned long g_hist[GPU_FRAME_HISTOGRAM_BUCKETS];
static unsigned long long g_recent[GPU_FRAME_TIMING_SAMPLE_CAPACITY];
static unsigned long long g_sorted[GPU_FRAME_TIMING_SAMPLE_CAPACITY];
static unsigned long g_recent_count, g_recent_next;

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

static int compare_ns(const void *left, const void *right)
{
    const unsigned long long a = *(const unsigned long long *)left;
    const unsigned long long b = *(const unsigned long long *)right;
    return a < b ? -1 : a > b;
}

static unsigned long percentile_index(unsigned long count, unsigned quantile)
{
    return (unsigned long)(((unsigned long long)count * quantile + 99u) / 100u - 1u);
}

void gpu_frame_timing_reset(void)
{
    g_frame_ns = 0;
    g_frame_ns_min = 0;
    g_frame_ns_max = 0;
    g_last_frame_end_ns = 0;
    g_frame_intervals = 0;
    memset(g_hist, 0, sizeof g_hist);
    g_recent_count = 0;
    g_recent_next = 0;
}

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
        g_recent[g_recent_next] = dt;
        g_recent_next = (g_recent_next + 1u) % GPU_FRAME_TIMING_SAMPLE_CAPACITY;
        if (g_recent_count < GPU_FRAME_TIMING_SAMPLE_CAPACITY) g_recent_count++;
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

void gpu_frame_timing_percentiles(unsigned long long *p50_ns,
                                  unsigned long long *p95_ns,
                                  unsigned long long *p99_ns,
                                  unsigned long *samples)
{
    const unsigned long count = g_recent_count;
    if (!p50_ns || !p95_ns || !p99_ns || !samples) return;
    *p50_ns = 0;
    *p95_ns = 0;
    *p99_ns = 0;
    *samples = count;
    if (!count) return;

    /* Status requests are rare and control_status already accepts torn
     * diagnostic reads; sorting a copied bounded window keeps frame-end O(1). */
    memcpy(g_sorted, g_recent, count * sizeof *g_sorted);
    qsort(g_sorted, count, sizeof *g_sorted, compare_ns);
    *p50_ns = g_sorted[percentile_index(count, 50u)];
    *p95_ns = g_sorted[percentile_index(count, 95u)];
    *p99_ns = g_sorted[percentile_index(count, 99u)];
}
