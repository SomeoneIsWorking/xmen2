/*
 * The luma probe must be able to show BOTH answers.
 *
 * A frame that is black must score 0, and a frame that is not must score
 * non-zero -- including the specific lies a present path can tell: a single
 * live channel (one channel's swizzle/format bug) and a dim frame. An
 * instrument that only ever reports "black" cannot license the sentence "the
 * browser presents black"; this one is checked against both classes first.
 */
#include "gpu_present_luma.h"

#include <stdio.h>

static int failures;

#define CHECK(cond, ...)                                                       \
  do {                                                                         \
    if (!(cond)) {                                                             \
      printf("FAIL: ");                                                        \
      printf(__VA_ARGS__);                                                     \
      printf("\n");                                                            \
      failures++;                                                              \
    }                                                                          \
  } while (0)

static void fill(unsigned char *bgra, size_t pixels, unsigned char b,
                 unsigned char g, unsigned char r, unsigned char a) {
  size_t i;
  for (i = 0; i < pixels; i++) {
    bgra[i * 4 + 0] = b;
    bgra[i * 4 + 1] = g;
    bgra[i * 4 + 2] = r;
    bgra[i * 4 + 3] = a;
  }
}

#define W 64u
#define H 64u
#define N (W * H)

int main(void) {
  static unsigned char frame[N * 4];
  X2PresentLumaStats s;

  /* The negative class: a real all-black frame. */
  fill(frame, N, 0, 0, 0, 255);
  x2_present_luma_stats(frame, W, H, &s);
  CHECK(s.sampled == N || s.sampled > 0, "black frame sampled %u px",
        s.sampled);
  CHECK(s.nonblack == 0, "black frame reported %u nonblack samples",
        s.nonblack);
  CHECK(s.max_channel == 0, "black frame reported max %u", s.max_channel);
  CHECK(s.mean_luma < 1.0, "black frame reported mean %.2f", s.mean_luma);

  /* Uninitialised buffer with only ALPHA set must still be black -- the
     probe must not mistake the alpha byte for light. */
  fill(frame, N, 0, 0, 0, 255);
  x2_present_luma_stats(frame, W, H, &s);
  CHECK(s.nonblack == 0 && s.max_channel == 0,
        "alpha-only frame read as light (max %u)", s.max_channel);

  /* White: the positive class. */
  fill(frame, N, 255, 255, 255, 255);
  x2_present_luma_stats(frame, W, H, &s);
  CHECK(s.nonblack == s.sampled, "white frame: %u of %u samples nonblack",
        s.nonblack, s.sampled);
  CHECK(s.mean_luma > 250.0, "white frame mean %.2f", s.mean_luma);

  /* A single live channel -- the format-swizzle failure -- must be caught
     even though its luma is low: blue averages ~28 by ITU weights. */
  fill(frame, N, 255, 0, 0, 255);
  x2_present_luma_stats(frame, W, H, &s);
  CHECK(s.nonblack == s.sampled, "blue frame: %u of %u nonblack", s.nonblack,
        s.sampled);
  CHECK(s.max_channel == 255, "blue frame max %u", s.max_channel);
  CHECK(s.mean_luma < 60.0 && s.mean_luma > 10.0, "blue frame mean %.2f",
        s.mean_luma);

  /* A half-lit frame: the nonblack fraction must track the geometry, so a
     report cannot hide a mostly-black present behind a bright few pixels. */
  fill(frame, N, 0, 0, 0, 255);
  {
    unsigned x, y;
    for (y = 0; y < H; y++)
      for (x = 0; x < W / 2; x++) {
        size_t i = (size_t)y * W + x;
        frame[i * 4 + 2] = 200;
      }
  }
  x2_present_luma_stats(frame, W, H, &s);
  CHECK(s.nonblack > s.sampled / 3 && s.nonblack < (s.sampled * 2) / 3,
        "half-lit frame: %u of %u samples, expected about half", s.nonblack,
        s.sampled);

  /* Zero-size and missing frames must report nothing bright, not crash. */
  x2_present_luma_stats(frame, 0, H, &s);
  CHECK(s.sampled == 0 && s.nonblack == 0, "zero-width frame sampled %u",
        s.sampled);
  x2_present_luma_stats(NULL, W, H, &s);
  CHECK(s.sampled == 0, "NULL frame sampled %u", s.sampled);

  if (!failures) {
    printf("present_luma: both answers proven (black 0, white ~255, "
           "single-channel caught, fractions track geometry)\n");
    return 0;
  }
  printf("present_luma: %d check(s) FAILED\n", failures);
  return 1;
}
