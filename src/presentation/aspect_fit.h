#ifndef X2_ASPECT_FIT_H
#define X2_ASPECT_FIT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uint32_t x;
  uint32_t y;
  uint32_t width;
  uint32_t height;
} X2AspectRect;

/* Centre an inner image inside an outer target without changing its aspect
   ratio. Returns zero for an invalid or unrepresentable size. */
int x2_aspect_fit(uint32_t outer_width, uint32_t outer_height,
                  uint32_t inner_width, uint32_t inner_height,
                  X2AspectRect *out);

#ifdef __cplusplus
}
#endif

#endif
