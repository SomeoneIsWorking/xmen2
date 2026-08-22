#ifndef X2_FMV_TIMING_H
#define X2_FMV_TIMING_H

#include <stdint.h>

typedef struct {
    double next_fallback;
    double frame_duration;
    double last_timestamp;
    unsigned timestamp_fallbacks;
    unsigned timestamp_clamps;
    int have_last;
} X2FmvTimeline;

void x2_fmv_timeline_init(X2FmvTimeline *timeline, double frame_rate);
double x2_fmv_timestamp(X2FmvTimeline *timeline, int64_t best_effort,
                        int64_t no_timestamp, int timebase_num,
                        int timebase_den, int64_t frame_duration);

#endif
