#include "movie_audio.h"

#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  float *samples;
  size_t capacity_frames;
  size_t read_frame;
  size_t queued_frames;
  int sample_rate;
  int active;
  int paused;
  double source_fraction;
  double played_seconds;
  unsigned long opens;
  unsigned long underruns;
  unsigned long long queued_total;
} MovieAudio;

static MovieAudio g_movie_audio;
static pthread_mutex_t g_movie_audio_lock = PTHREAD_MUTEX_INITIALIZER;

static int reserve_frames(size_t required) {
  float *next;
  size_t capacity = g_movie_audio.capacity_frames;
  size_t i;
  if (required <= capacity)
    return 1;
  if (!capacity)
    capacity = 4096;
  while (capacity < required)
    capacity *= 2;
  next = (float *)malloc(capacity * 2 * sizeof(*next));
  if (!next)
    return 0;
  for (i = 0; i < g_movie_audio.queued_frames; ++i) {
    size_t old = (g_movie_audio.read_frame + i) % g_movie_audio.capacity_frames;
    next[i * 2] = g_movie_audio.samples[old * 2];
    next[i * 2 + 1] = g_movie_audio.samples[old * 2 + 1];
  }
  free(g_movie_audio.samples);
  g_movie_audio.samples = next;
  g_movie_audio.capacity_frames = capacity;
  g_movie_audio.read_frame = 0;
  return 1;
}

int movie_audio_open(int sample_rate) {
  int ok;
  if (sample_rate <= 0)
    return 0;
  pthread_mutex_lock(&g_movie_audio_lock);
  g_movie_audio.read_frame = 0;
  g_movie_audio.queued_frames = 0;
  g_movie_audio.sample_rate = sample_rate;
  g_movie_audio.active = 1;
  g_movie_audio.paused = 1;
  g_movie_audio.source_fraction = 0.0;
  g_movie_audio.played_seconds = 0.0;
  g_movie_audio.opens++;
  ok = reserve_frames((size_t)sample_rate);
  if (!ok)
    g_movie_audio.active = 0;
  pthread_mutex_unlock(&g_movie_audio_lock);
  return ok;
}

int movie_audio_queue(const float *stereo, size_t frames) {
  size_t i;
  int ok = 0;
  pthread_mutex_lock(&g_movie_audio_lock);
  if (!g_movie_audio.active || (!stereo && frames))
    goto done;
  if (!reserve_frames(g_movie_audio.queued_frames + frames))
    goto done;
  for (i = 0; i < frames; ++i) {
    size_t dst = (g_movie_audio.read_frame + g_movie_audio.queued_frames + i) %
                 g_movie_audio.capacity_frames;
    g_movie_audio.samples[dst * 2] = stereo[i * 2];
    g_movie_audio.samples[dst * 2 + 1] = stereo[i * 2 + 1];
  }
  g_movie_audio.queued_frames += frames;
  g_movie_audio.queued_total += frames;
  ok = 1;
done:
  pthread_mutex_unlock(&g_movie_audio_lock);
  return ok;
}

void movie_audio_play(void) {
  pthread_mutex_lock(&g_movie_audio_lock);
  if (g_movie_audio.active)
    g_movie_audio.paused = 0;
  pthread_mutex_unlock(&g_movie_audio_lock);
}

void movie_audio_pause(int paused) {
  pthread_mutex_lock(&g_movie_audio_lock);
  if (g_movie_audio.active)
    g_movie_audio.paused = paused ? 1 : 0;
  pthread_mutex_unlock(&g_movie_audio_lock);
}

void movie_audio_close(void) {
  pthread_mutex_lock(&g_movie_audio_lock);
  g_movie_audio.active = 0;
  g_movie_audio.paused = 1;
  g_movie_audio.queued_frames = 0;
  g_movie_audio.read_frame = 0;
  g_movie_audio.source_fraction = 0.0;
  pthread_mutex_unlock(&g_movie_audio_lock);
}

static float queued_sample(size_t frame, int channel) {
  size_t index =
      (g_movie_audio.read_frame + frame) % g_movie_audio.capacity_frames;
  return g_movie_audio.samples[index * 2 + (size_t)channel];
}

void movie_audio_mix(float *mix, int frames, int output_rate) {
  double step;
  int i;
  pthread_mutex_lock(&g_movie_audio_lock);
  if (!g_movie_audio.active || g_movie_audio.paused || frames <= 0 ||
      output_rate <= 0)
    goto done;
  step = (double)g_movie_audio.sample_rate / (double)output_rate;
  for (i = 0; i < frames; ++i) {
    size_t pos = (size_t)g_movie_audio.source_fraction;
    float fraction = (float)(g_movie_audio.source_fraction - (double)pos);
    if (pos >= g_movie_audio.queued_frames) {
      g_movie_audio.underruns++;
      g_movie_audio.played_seconds += 1.0 / (double)output_rate;
      continue;
    }
    if (mix) {
      size_t next = pos + 1 < g_movie_audio.queued_frames ? pos + 1 : pos;
      int channel;
      for (channel = 0; channel < 2; ++channel) {
        float a = queued_sample(pos, channel);
        float b = queued_sample(next, channel);
        mix[i * 2 + channel] += a + (b - a) * fraction;
      }
    }
    g_movie_audio.source_fraction += step;
    if (g_movie_audio.source_fraction >= 1.0) {
      size_t consumed = (size_t)g_movie_audio.source_fraction;
      if (consumed > g_movie_audio.queued_frames)
        consumed = g_movie_audio.queued_frames;
      g_movie_audio.read_frame =
          (g_movie_audio.read_frame + consumed) % g_movie_audio.capacity_frames;
      g_movie_audio.queued_frames -= consumed;
      g_movie_audio.source_fraction -= (double)consumed;
    }
    g_movie_audio.played_seconds += 1.0 / (double)output_rate;
  }
done:
  pthread_mutex_unlock(&g_movie_audio_lock);
}

double movie_audio_played_seconds(void) {
  double seconds;
  pthread_mutex_lock(&g_movie_audio_lock);
  seconds = g_movie_audio.played_seconds;
  pthread_mutex_unlock(&g_movie_audio_lock);
  return seconds;
}

double movie_audio_queued_seconds(void) {
  double seconds;
  pthread_mutex_lock(&g_movie_audio_lock);
  seconds = g_movie_audio.sample_rate <= 0
                ? 0.0
                : (double)g_movie_audio.queued_frames /
                      (double)g_movie_audio.sample_rate;
  pthread_mutex_unlock(&g_movie_audio_lock);
  return seconds;
}

int movie_audio_active(void) {
  int active;
  pthread_mutex_lock(&g_movie_audio_lock);
  active = g_movie_audio.active;
  pthread_mutex_unlock(&g_movie_audio_lock);
  return active;
}

void movie_audio_report(void) {
  pthread_mutex_lock(&g_movie_audio_lock);
  printf("          movie audio: %lu open(s), %llu frame(s) queued, "
         "%lu underrun output frame(s)%s\n",
         g_movie_audio.opens, g_movie_audio.queued_total,
         g_movie_audio.underruns, g_movie_audio.active ? " -- ACTIVE" : "");
  pthread_mutex_unlock(&g_movie_audio_lock);
}
