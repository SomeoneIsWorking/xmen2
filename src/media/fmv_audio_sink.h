#pragma once

#include <cstddef>

namespace x2::media {

struct X2FmvAudioSink {
  void *userdata;
  int (*queue_stereo_f32)(void *userdata, const float *samples, size_t frames,
                          int sample_rate);
  double (*queued_seconds)(void *userdata);
};

} // namespace x2::media
