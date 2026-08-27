#ifndef X2_GPU_TEXTURE_FORMAT_H
#define X2_GPU_TEXTURE_FORMAT_H

#include <stdint.h>

/* D3DFMT_R8G8B8 is stored as B, G, R bytes on little-endian x86. SDL_GPU
   has no portable 24-bit sampled format, so uploads expand it to BGRA8. */
void gpu_bgr8_to_bgra8(const uint8_t *source, uint8_t *destination,
                       uint32_t pixels);

#endif
