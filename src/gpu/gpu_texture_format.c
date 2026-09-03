#include "gpu_texture_format.h"

void gpu_bgr8_to_bgra8(const uint8_t *source, uint8_t *destination,
                       uint32_t pixels) {
  uint32_t i;
  for (i = 0; i < pixels; i++) {
    destination[i * 4u + 0u] = source[i * 3u + 0u];
    destination[i * 4u + 1u] = source[i * 3u + 1u];
    destination[i * 4u + 2u] = source[i * 3u + 2u];
    destination[i * 4u + 3u] = 0xffu;
  }
}
