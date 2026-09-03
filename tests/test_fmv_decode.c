#include "fmv_player.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  unsigned long long frames;
  int sample_rate;
} AudioCounter;

static int count_audio(void *userdata, const float *samples, size_t frames,
                       int sample_rate) {
  AudioCounter *counter = (AudioCounter *)userdata;
  if (!samples && frames)
    return 0;
  counter->frames += frames;
  counter->sample_rate = sample_rate;
  return 1;
}

static double empty_audio_queue(void *userdata) {
  (void)userdata;
  return 0.0;
}

static uint64_t hash_bytes(const uint8_t *bytes, size_t size) {
  uint64_t hash = UINT64_C(1469598103934665603);
  size_t i;
  for (i = 0; i < size; ++i) {
    hash ^= bytes[i];
    hash *= UINT64_C(1099511628211);
  }
  return hash;
}

static int lower_half_has_picture(const uint8_t *pixels, int width,
                                  int height) {
  unsigned minimum = 765;
  unsigned maximum = 0;
  int x, y;
  for (y = height / 2; y < height; y += 4) {
    for (x = 0; x < width; x += 4) {
      const uint8_t *pixel = pixels + ((size_t)y * width + x) * 4u;
      unsigned value = pixel[0] + pixel[1] + pixel[2];
      if (value < minimum)
        minimum = value;
      if (value > maximum)
        maximum = value;
    }
  }
  return maximum - minimum > 48;
}

static int padding_is_untouched(const uint8_t *pixels, int width, int height,
                                size_t pitch) {
  size_t row_bytes = (size_t)width * 4u;
  int y;
  for (y = 0; y < height; ++y) {
    size_t offset;
    for (offset = row_bytes; offset < pitch; ++offset)
      if (pixels[(size_t)y * pitch + offset] != 0xa5)
        return 0;
  }
  return 1;
}

static const char *base_name(const char *path) {
  const char *slash = strrchr(path, '/');
  return slash ? slash + 1 : path;
}

static int parse_expected_frames(const char *text, unsigned long *frames) {
  char *end;
  unsigned long parsed;
  if (!text) {
    *frames = 0;
    return 1;
  }
  errno = 0;
  parsed = strtoul(text, &end, 10);
  if (errno || end == text || *end || !parsed)
    return 0;
  *frames = parsed;
  return 1;
}

int main(int argc, char **argv) {
  const char *path = argc > 1 ? argv[1] : getenv("X2_TEST_SFD");
  const char *video_frames_text = getenv("X2_TEST_SFD_VIDEO_FRAMES");
  const char *audio_frames_text = getenv("X2_TEST_SFD_AUDIO_FRAMES");
  X2FmvAudioSink sink;
  AudioCounter audio = {0};
  X2FmvPlayer *player;
  uint8_t *tight = NULL;
  uint8_t *pitched = NULL;
  size_t tight_size, pitch, pitched_size;
  uint64_t prior_hash = 0;
  int changed_frames = 0, distinct_frames = 0, picture_frames = 0;
  unsigned long expected_video;
  unsigned long expected_audio;
  int width, height, step, y, failures = 0;
  char error[256] = {0};
  if (!path || !*path) {
    printf("FMV decode: SKIPPED -- set X2_TEST_SFD to a user-provided "
           "shipped .sfd outside git\n");
    return 77;
  }
  if (!parse_expected_frames(video_frames_text, &expected_video) ||
      !parse_expected_frames(audio_frames_text, &expected_audio)) {
    fprintf(stderr, "FMV decode: FAILED -- expected frame counts must be "
                    "positive decimal integers\n");
    return 1;
  }
  sink.userdata = &audio;
  sink.queue_stereo_f32 = count_audio;
  sink.queued_seconds = empty_audio_queue;
  player = x2_fmv_open(path, &sink, error, sizeof(error));
  if (!player) {
    fprintf(stderr, "FMV decode: FAILED -- %s\n", error);
    return 1;
  }
  width = x2_fmv_width(player);
  height = x2_fmv_height(player);
  tight_size = (size_t)width * height * 4u;
  pitch = (size_t)width * 4u + 64u;
  pitched_size = pitch * (size_t)height;
  tight = (uint8_t *)malloc(tight_size);
  pitched = (uint8_t *)malloc(pitched_size);
  if (!tight || !pitched)
    failures++;
  x2_fmv_play(player);
  for (step = 0; !failures && step < 180; ++step) {
    int changed = x2_fmv_update(player, (double)step / 30.0);
    uint64_t hash;
    if (changed < 0) {
      failures++;
      break;
    }
    if (!changed)
      continue;
    memset(pitched, 0xa5, pitched_size);
    failures +=
        !x2_fmv_copy_bgra(player, tight, tight_size, (size_t)width * 4u);
    failures += !x2_fmv_copy_bgra(player, pitched, pitched_size, pitch);
    for (y = 0; y < height; ++y) {
      failures += memcmp(tight + (size_t)y * width * 4u,
                         pitched + (size_t)y * pitch, (size_t)width * 4u) != 0;
    }
    failures += !padding_is_untouched(pitched, width, height, pitch);
    hash = hash_bytes(tight, tight_size);
    if (changed_frames && hash != prior_hash)
      distinct_frames++;
    prior_hash = hash;
    picture_frames += lower_half_has_picture(tight, width, height);
    changed_frames++;
  }
  for (step = 0;
       !failures && x2_fmv_state(player) != X2_FMV_FINISHED && step < 1000;
       ++step) {
    if (x2_fmv_update(player, 1000000.0 + step) < 0)
      failures++;
  }
  failures += width != 640 || height != 480;
  failures += x2_fmv_sample_rate(player) != 44100;
  failures += changed_frames < 90 || distinct_frames < 30;
  failures += picture_frames == 0;
  failures += audio.frames < 44100 || audio.sample_rate != 44100;
  failures += x2_fmv_state(player) != X2_FMV_FINISHED;
  failures += expected_video && x2_fmv_decoded_frames(player) != expected_video;
  failures +=
      expected_audio && x2_fmv_decoded_audio_frames(player) != expected_audio;
  failures += audio.frames != x2_fmv_decoded_audio_frames(player);
  printf("FMV decode: %s -- %s, %dx%d, %d changed / %d distinct frames; "
         "all %d rows match tight and padded copies; drained %lu video / "
         "%llu audio frames%s\n",
         failures ? "FAILED" : "PASSED", base_name(path), width, height,
         changed_frames, distinct_frames, height, x2_fmv_decoded_frames(player),
         audio.frames,
         expected_video && expected_audio ? " at source parity" : "");
  free(pitched);
  free(tight);
  x2_fmv_close(player);
  return failures != 0;
}
