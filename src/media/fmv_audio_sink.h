#ifndef X2_FMV_AUDIO_SINK_H
#define X2_FMV_AUDIO_SINK_H

#include <stddef.h>

typedef struct {
  void *userdata;
  int (*queue_stereo_f32)(void *userdata, const float *samples, size_t frames,
                          int sample_rate);
  double (*queued_seconds)(void *userdata);
} X2FmvAudioSink;

#endif
