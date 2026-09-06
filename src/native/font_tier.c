#include "font_tier.h"

#include <stdlib.h>

static int compare_ratio(const void *a, const void *b) {
  const float left = *(const float *)a;
  const float right = *(const float *)b;
  if (left < right)
    return -1;
  return left > right ? 1 : 0;
}

float x2_font_tier_median(float *ratios, unsigned count) {
  if (!ratios || !count)
    return 0.0f;
  qsort(ratios, count, sizeof *ratios, compare_ratio);
  if (count % 2u)
    return ratios[count / 2u];
  return (ratios[count / 2u - 1u] + ratios[count / 2u]) / 2.0f;
}

float x2_font_tier_ratio(const int16_t *pc_heights, const int16_t *hd_heights,
                         unsigned count, unsigned *samples) {
  float ratios[X2_FONT_TIER_SAMPLES];
  unsigned drawn = 0;
  unsigned i;
  float middle;

  if (!pc_heights || !hd_heights || count > X2_FONT_TIER_SAMPLES)
    return 0.0f;
  for (i = 0; i < count; i++) {
    if (pc_heights[i] <= 0 || hd_heights[i] <= 0)
      continue;
    ratios[drawn++] = (float)hd_heights[i] / (float)pc_heights[i];
  }
  if (samples)
    *samples = drawn;
  if (drawn < count / 2u || !drawn)
    return 0.0f;
  middle = x2_font_tier_median(ratios, drawn);
  if (middle < 1.0f || middle > 4.0f)
    return 0.0f;
  return middle;
}
