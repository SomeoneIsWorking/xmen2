#include "resolution_ladder.h"

#include <stdio.h>

static const unsigned kHeights[] = {720u, 1080u, 1440u, 2160u};
static const size_t kHeightCount = sizeof kHeights / sizeof kHeights[0];

unsigned x2_resolution_width_for(unsigned height, unsigned display_w,
                                 unsigned display_h) {
  unsigned width;
  if (!height)
    return 0;
  if (!display_w || !display_h) {
    display_w = 16u;
    display_h = 9u;
  }
  /* Round to nearest, then to an even width: an odd width breaks chroma
     subsampling in the FMV path and is not a ratio the panel can honour
     anyway. */
  width = (unsigned)(((unsigned long long)height * display_w + display_h / 2u) /
                     display_h);
  return width & ~1u;
}

unsigned x2_resolution_next_height(unsigned height, unsigned display_h) {
  size_t i;
  for (i = 0; i < kHeightCount; i++) {
    if (kHeights[i] != height)
      continue;
    /* Found the current preset: walk forward to the next one the display can
       show, wrapping. The first entry always qualifies, so this terminates. */
    for (size_t step = 1; step <= kHeightCount; step++) {
      unsigned next = kHeights[(i + step) % kHeightCount];
      if (next == kHeights[0] || !display_h || next <= display_h)
        return next;
    }
  }
  return kHeights[0];
}

size_t x2_resolution_label(unsigned height, char *buf, size_t size) {
  int n;
  if (!buf || !size)
    return 0;
  n = snprintf(buf, size, "%up", height);
  if (n < 0 || (size_t)n >= size) {
    buf[0] = '\0';
    return 0;
  }
  return (size_t)n;
}
