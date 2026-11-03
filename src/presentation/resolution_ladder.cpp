#include "resolution_ladder.h"

#include "settings.h"

#include <stdio.h>

namespace x2::presentation {

static const unsigned kChoices[] = {x2::config::kRenderNative, 720u, 1080u,
                                    1440u, 2160u};
static const size_t kChoiceCount = sizeof kChoices / sizeof kChoices[0];

unsigned resolution_width_for(unsigned height, unsigned display_w,
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

static bool offered(unsigned lines, unsigned display_h) {
  return lines == x2::config::kRenderNative || lines == 720u || !display_h ||
         lines <= display_h;
}

unsigned resolution_step_lines(unsigned lines, unsigned display_h,
                               int direction) {
  for (size_t i = 0; i < kChoiceCount; i++) {
    if (kChoices[i] != lines)
      continue;
    for (size_t step = 1; step <= kChoiceCount; step++) {
      size_t at = direction < 0 ? i + kChoiceCount - step : i + step;
      unsigned next = kChoices[at % kChoiceCount];
      if (offered(next, display_h))
        return next;
    }
  }
  return x2::config::kRenderNative;
}

size_t resolution_label(unsigned lines, char *buf, size_t size) {
  int n;
  if (!buf || !size)
    return 0;
  if (lines == x2::config::kRenderNative)
    n = snprintf(buf, size, "Native");
  else
    n = snprintf(buf, size, "%up", lines);
  if (n < 0 || (size_t)n >= size) {
    buf[0] = '\0';
    return 0;
  }
  return (size_t)n;
}

} // namespace x2::presentation
