#include "fmv_timing.h"

#include <math.h>
#include <string.h>

void x2_fmv_timeline_init(X2FmvTimeline *timeline, double frame_rate)
{
    memset(timeline, 0, sizeof(*timeline));
    timeline->frame_duration = frame_rate > 0.0 ? 1.0 / frame_rate : 1.0 / 30.0;
}

double x2_fmv_timestamp(X2FmvTimeline *timeline, int64_t best_effort,
                        int64_t no_timestamp, int timebase_num,
                        int timebase_den, int64_t frame_duration)
{
    double timestamp;
    double duration = timeline->frame_duration;
    if (frame_duration > 0 && timebase_num > 0 && timebase_den > 0)
        duration = (double)frame_duration * (double)timebase_num
                 / (double)timebase_den;
    if (duration <= 0.0 || !isfinite(duration)) duration = 1.0 / 30.0;
    if (best_effort == no_timestamp || timebase_num <= 0 || timebase_den <= 0) {
        timestamp = timeline->next_fallback;
        timeline->timestamp_fallbacks++;
    } else {
        timestamp = (double)best_effort * (double)timebase_num
                  / (double)timebase_den;
        if (!isfinite(timestamp) || timestamp < 0.0) {
            timestamp = timeline->next_fallback;
            timeline->timestamp_fallbacks++;
        }
    }
    if (timeline->have_last && timestamp <= timeline->last_timestamp) {
        timestamp = timeline->next_fallback;
        timeline->timestamp_clamps++;
    }
    timeline->frame_duration = duration;
    timeline->last_timestamp = timestamp;
    timeline->next_fallback = timestamp + duration;
    timeline->have_last = 1;
    return timestamp;
}
