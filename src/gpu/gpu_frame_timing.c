#include "gpu_frame_timing.h"

int gpu_frame_timing_bucket(unsigned long long nanoseconds)
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
