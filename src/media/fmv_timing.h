#pragma once

#include <cstdint>

namespace x2::media {

struct FmvTimeline {
  double next_fallback;
  double frame_duration;
  double last_timestamp;
  unsigned timestamp_fallbacks;
  unsigned timestamp_clamps;
  int have_last;
};

void fmv_timeline_init(FmvTimeline *timeline, double frame_rate);
double fmv_timestamp(FmvTimeline *timeline, int64_t best_effort,
                     int64_t no_timestamp, int timebase_num, int timebase_den,
                     int64_t frame_duration);

} // namespace x2::media
