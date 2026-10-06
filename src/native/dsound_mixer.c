#include "x2_log.h"
/*
 * The DirectSound voice registry, software mixer and host playback stream.
 * See dsound_mixer.h for the boundary; dsound.c owns the COM surface above
 * it.
 */
#include "dsound_mixer.h"
#include "guest_clock.h"
#include "guest_heap.h"
#include "guest_memory.h"
#include "movie_audio.h"
#include "win32_sdl.h"
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#ifdef X2_WITH_SDL
#include <SDL3/SDL.h>
#endif

static DSBuffer *g_buf;
static int g_nbuf, g_capbuf;
static unsigned long g_mix_callbacks, g_mix_frames, g_silent_advances;
static unsigned long g_mix_nonzero;
static float g_mix_peak;
static int g_primary_rate = 22050;
static int g_audio_attempted, g_audio_silent;
static double g_silent_time;

#ifdef X2_WITH_SDL
static SDL_AudioStream *g_stream;
static float *g_mix_scratch;
static int g_mix_scratch_frames;
#endif

/* The guest's clock, not a private one: see guest_clock.h. Five copies of
   this read CLOCK_MONOTONIC directly, and the guest gates real logic on
   elapsed time, so any two of them disagreeing is a timing bug wearing a
   gameplay bug's clothes. */
static double now_s(void) { return guest_clock_now_s(); }

void dsound_mixer_lock(void) {
#ifdef X2_WITH_SDL
  if (g_stream && SDL_WasInit(SDL_INIT_AUDIO))
    SDL_LockAudioStream(g_stream);
#endif
}

void dsound_mixer_unlock(void) {
#ifdef X2_WITH_SDL
  if (g_stream && SDL_WasInit(SDL_INIT_AUDIO))
    SDL_UnlockAudioStream(g_stream);
#endif
}
static float sample_at(const DSBuffer *b, uint64_t frame, int channel) {
  const unsigned char *p;
  uint64_t frames;
  int srcch;
  if (!b->data || !b->data->guest_data || !b->block_align)
    return 0.0f;
  frames = b->data->bytes / b->block_align;
  if (!frames)
    return 0.0f;
  frame %= frames;
  srcch = b->channels == 1 ? 0 : channel;
  if (srcch >= b->channels)
    srcch = b->channels - 1;
  p = (const unsigned char *)guest_memory_const_pointer(b->data->guest_data) +
      frame * b->block_align + (uint64_t)srcch * (b->bits / 8u);
  if (b->bits == 8)
    return ((float)p[0] - 128.0f) / 128.0f;
  if (b->bits == 16) {
    int16_t s;
    memcpy(&s, p, sizeof s);
    return (float)s / 32768.0f;
  }
  return 0.0f;
}

static void advance_buffer(DSBuffer *b, double out_frames, double out_rate,
                           float *mix) {
  uint64_t nsrc;
  double step;
  float gain, gl, gr;
  int i;
  if (!b->playing || b->primary || !b->data || !b->block_align)
    return;
  nsrc = b->data->bytes / b->block_align;
  if (!nsrc) {
    b->playing = 0;
    return;
  }
  step = (double)(b->frequency ? b->frequency : b->sample_rate) / out_rate;
  gain = b->volume <= -10000 ? 0.0f : powf(10.0f, (float)b->volume / 2000.0f);
  gl = gr = gain;
  if (b->pan > 0)
    gl *= powf(10.0f, -(float)b->pan / 2000.0f);
  if (b->pan < 0)
    gr *= powf(10.0f, (float)b->pan / 2000.0f);
  for (i = 0; i < (int)out_frames; ++i) {
    uint64_t pos = (uint64_t)b->cursor_frames;
    if (pos >= nsrc) {
      if (!b->looping) {
        b->playing = 0;
        break;
      }
      b->cursor_frames = fmod(b->cursor_frames, (double)nsrc);
      pos = (uint64_t)b->cursor_frames;
    }
    if (mix) {
      uint64_t next = pos + 1u;
      float frac = (float)(b->cursor_frames - (double)pos);
      float l0, l1, r0, r1;
      if (next >= nsrc)
        next = b->looping ? 0u : pos;
      l0 = sample_at(b, pos, 0);
      l1 = sample_at(b, next, 0);
      r0 = sample_at(b, pos, 1);
      r1 = sample_at(b, next, 1);
      mix[i * 2 + 0] += (l0 + (l1 - l0) * frac) * gl;
      mix[i * 2 + 1] += (r0 + (r1 - r0) * frac) * gr;
    }
    b->cursor_frames += step;
  }
}

static void mix_frames(float *mix, int frames, int rate) {
  int i;
  if (mix)
    memset(mix, 0, (size_t)frames * 2u * sizeof(float));
  for (i = 0; i < g_nbuf; ++i)
    if (g_buf[i].used)
      advance_buffer(&g_buf[i], frames, rate, mix);
  movie_audio_mix(mix, frames, rate);
  if (mix) {
    for (i = 0; i < frames * 2; ++i) {
      if (mix[i] > 1.0f)
        mix[i] = 1.0f;
      if (mix[i] < -1.0f)
        mix[i] = -1.0f;
      if (fabsf(mix[i]) > g_mix_peak)
        g_mix_peak = fabsf(mix[i]);
      if (mix[i] != 0.0f)
        g_mix_nonzero++;
    }
  }
}
#ifdef X2_WITH_SDL
static void SDLCALL audio_more(void *userdata, SDL_AudioStream *stream,
                               int additional_amount, int total_amount) {
  int frames = additional_amount / (int)(2u * sizeof(float));
  float *mix;
  (void)userdata;
  (void)total_amount;
  if (frames <= 0)
    return;
  if (frames > g_mix_scratch_frames) {
    float *next =
        (float *)realloc(g_mix_scratch, (size_t)frames * 2u * sizeof(float));
    if (!next)
      return;
    g_mix_scratch = next;
    g_mix_scratch_frames = frames;
  }
  mix = g_mix_scratch;
  mix_frames(mix, frames, g_primary_rate);
  SDL_PutAudioStreamData(stream, mix, frames * 2 * (int)sizeof(float));
  g_mix_callbacks++;
  g_mix_frames += (unsigned long)frames;
}
#endif
void dsound_mixer_open_device(void) {
  if (g_audio_attempted)
    return;
  g_audio_attempted = 1;
  /*
   * A run with no window is a run nobody is listening to: an automated or
   * observational run should not seize the machine's speakers and talk over
   * whatever the user is actually doing.
   *
   * This takes the SILENT-BUT-TIMED device rather than skipping audio, and
   * the difference matters. The game drives real logic off buffer play
   * cursors -- a cutscene advances when its stream reports itself finished --
   * so a device whose cursors never move does not make the run quiet, it
   * makes the run hang. The silent device below advances every cursor on the
   * wall clock at the buffer's own rate, so the guest sees audio complete on
   * schedule and hears nothing.
   */
  if (win32_sdl_windows_hidden()) {
    x2_log_error("DSOUND: --no-window, so no host playback device is "
                 "opened -- using the timed SILENT device. Play cursors "
                 "still advance at %d Hz, so audio-gated logic (cutscene "
                 "advance, stream-complete waits) runs exactly as it "
                 "does with sound.\n",
                 g_primary_rate);
    g_audio_silent = 1;
    g_silent_time = now_s();
    return;
  }
#ifdef X2_WITH_SDL
  {
    SDL_AudioSpec spec;
    if (!SDL_WasInit(SDL_INIT_AUDIO) && !SDL_InitSubSystem(SDL_INIT_AUDIO)) {
      x2_log_error("DSOUND: SDL audio init failed: %s -- using a "
                   "timed SILENT device; cursors still advance.\n",
                   SDL_GetError());
      g_audio_silent = 1;
      g_silent_time = now_s();
      return;
    }
    SDL_zero(spec);
    spec.format = SDL_AUDIO_F32;
    spec.channels = 2;
    spec.freq = g_primary_rate;
    g_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                                         &spec, audio_more, NULL);
    if (!g_stream || !SDL_ResumeAudioStreamDevice(g_stream)) {
      x2_log_error("DSOUND: no host playback stream: %s -- using a "
                   "timed SILENT device; cursors still advance.\n",
                   SDL_GetError());
      if (g_stream)
        SDL_DestroyAudioStream(g_stream);
      g_stream = NULL;
      g_audio_silent = 1;
      g_silent_time = now_s();
      return;
    }
    x2_log_error("DSOUND: SDL3 playback opened at %d Hz, stereo F32; "
                 "the game supplies PCM through DirectSound buffers.\n",
                 g_primary_rate);
  }
#else
  x2_log_error("DSOUND: built without SDL audio -- using a timed SILENT "
               "device; cursors still advance.\n");
  g_audio_silent = 1;
  g_silent_time = now_s();
#endif
}
void dsound_mixer_tick_silent(void) {
  double t, elapsed;
  if (!g_audio_silent)
    return;
  t = now_s();
  elapsed = t - g_silent_time;
  if (elapsed <= 0.0)
    return;
  g_silent_time = t;
  mix_frames(NULL, (int)(elapsed * g_primary_rate), g_primary_rate);
  g_silent_advances++;
}

int dsound_mixer_rate(void) { return g_primary_rate; }

void dsound_mixer_set_rate(int hz) { g_primary_rate = hz; }

DSBuffer *dsound_mixer_voice_of(uint32_t guest) {
  int i;
  for (i = 0; i < g_nbuf; ++i) {
    if (g_buf[i].used && g_buf[i].guest == guest) {
      return &g_buf[i];
    }
  }
  return NULL;
}

static DSBuffer *claim(DSBuffer *b) {
  memset(b, 0, sizeof *b);
  b->used = 1;
  b->refs = 1;
  b->volume = 0;
  return b;
}

DSBuffer *dsound_mixer_alloc_voice(void) {
  int i;
  for (i = 0; i < g_nbuf; ++i) {
    if (!g_buf[i].used) {
      return claim(&g_buf[i]);
    }
  }
  if (g_nbuf == g_capbuf) {
    int cap = g_capbuf ? g_capbuf * 2 : 64;
    DSBuffer *next = (DSBuffer *)realloc(g_buf, (size_t)cap * sizeof *next);
    if (!next) {
      return NULL;
    }
    memset(next + g_capbuf, 0, (size_t)(cap - g_capbuf) * sizeof *next);
    g_buf = next;
    g_capbuf = cap;
  }
  return claim(&g_buf[g_nbuf++]);
}

void dsound_mixer_free_voice(DSBuffer *b) {
  if (b->data && --b->data->refs == 0) {
    if (b->data->guest_data) {
      guest_free(b->data->guest_data);
    }
    free(b->data);
  }
  b->data = NULL;
  b->playing = 0;
  b->used = 0;
}

void dsound_mixer_voice_counts(int *live, int *playing) {
  int i;
  *live = 0;
  *playing = 0;
  for (i = 0; i < g_nbuf; ++i) {
    if (g_buf[i].used) {
      (*live)++;
      if (g_buf[i].playing) {
        (*playing)++;
      }
    }
  }
}

void dsound_mixer_stats(DsoundMixerStats *out) {
  out->callbacks = g_mix_callbacks;
  out->frames = g_mix_frames;
  out->nonzero = g_mix_nonzero;
  out->silent_advances = g_silent_advances;
  out->peak = g_mix_peak;
  out->attempted = g_audio_attempted;
  out->silent = g_audio_silent;
}

int dsound_mixer_selftest(void) {
  DSBuffer a, b;
  SampleData d;
  unsigned char pcm[8] = {0, 0, 0, 64, 0, 128, 0, 192};
  float mix[8];
  int fails = 0, i;
  memset(&a, 0, sizeof a);
  memset(&b, 0, sizeof b);
  memset(&d, 0, sizeof d);
  d.guest_data = guest_malloc(sizeof pcm);
  d.bytes = sizeof pcm;
  d.refs = 2;
  if (!d.guest_data) {
    x2_log_info("DSOUND mixer selftest: FAILED -- no guest PCM allocation\n");
    return 1;
  }
  memcpy(guest_memory_pointer(d.guest_data), pcm, sizeof pcm);
  a.used = b.used = 1;
  a.data = b.data = &d;
  a.channels = b.channels = 1;
  a.bits = b.bits = 16;
  a.block_align = b.block_align = 2;
  a.sample_rate = b.sample_rate = 4;
  a.frequency = b.frequency = 4;
  a.playing = b.playing = 1;
  a.looping = b.looping = 1;
  b.volume = -10000;
  memset(mix, 0, sizeof mix);
  advance_buffer(&a, 4, 4, mix);
  advance_buffer(&b, 4, 4, mix);
  if (a.cursor_frames != 4.0 || b.cursor_frames != 4.0 ||
      fabsf(mix[0]) > 0.0001f || mix[2] < 0.49f || mix[4] > -0.99f ||
      mix[6] > -0.49f) {
    fails++;
  }
  a.cursor_frames = 3.0;
  a.playing = 1;
  a.looping = 0;
  advance_buffer(&a, 2, 4, NULL);
  if (a.playing || a.cursor_frames != 4.0) {
    fails++;
  }
  guest_free(d.guest_data);
  /* Loading the tutorial uses more than 256 COM buffer objects. This drives
     the exact old failure class instead of testing a tiny happy path. */
  if (g_nbuf != 0) {
    fails++;
  }
  for (i = 0; i < 300; i++) {
    if (!dsound_mixer_alloc_voice()) {
      fails++;
      break;
    }
  }
  if (g_nbuf != 300 || g_capbuf < 300) {
    fails++;
  }
  free(g_buf);
  g_buf = NULL;
  g_nbuf = g_capbuf = 0;
  x2_log_info(
      "DSOUND mixer selftest: %s -- shared PCM voices have independent "
      "cursors, mute and one-shot stop semantics; the registry grows past "
      "256 objects\n",
      fails ? "FAILED" : "PASSED");
  return fails;
}
