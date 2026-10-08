#pragma once

#include <cstdint>

namespace x2::presentation {

struct AspectRect {
  uint32_t x;
  uint32_t y;
  uint32_t width;
  uint32_t height;
};

/* Centre an inner image inside an outer target without changing its aspect
   ratio. Returns zero for an invalid or unrepresentable size. */
int aspect_fit(uint32_t outer_width, uint32_t outer_height,
               uint32_t inner_width, uint32_t inner_height, AspectRect *out);

} // namespace x2::presentation
