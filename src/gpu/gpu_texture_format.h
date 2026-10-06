#ifndef X2_GPU_TEXTURE_FORMAT_H
#define X2_GPU_TEXTURE_FORMAT_H

#include "gpu_draw.h"

#include <stdint.h>

/* D3DFMT_R8G8B8 is stored as B, G, R bytes on little-endian x86. SDL_GPU
   has no portable 24-bit sampled format, so uploads expand it to BGRA8. */
void gpu_bgr8_to_bgra8(const uint8_t *source, uint8_t *destination,
                       uint32_t pixels);

/* The format's name for a report: "BC1/DXT1", not an enum value. */
const char *gpu_texture_format_name(GpuFormat format);

/* Bytes in one mip level of `width` x `height`, as the GPU stores it: BGR8
   takes four bytes per pixel once expanded. 0 for an unknown format. */
uint32_t gpu_texture_level_bytes(GpuFormat format, uint32_t width,
                                 uint32_t height);

#ifdef X2_WITH_SDL
#include <SDL3/SDL.h>

/* The SDL_GPU format a GpuFormat is sampled as; INVALID for none. */
SDL_GPUTextureFormat gpu_texture_sdl_format(GpuFormat format);
#endif

#endif
