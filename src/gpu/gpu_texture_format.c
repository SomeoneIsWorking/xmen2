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

const char *gpu_texture_format_name(GpuFormat f) {
  switch (f) {
  case GPU_FMT_BGRA8:
    return "BGRA8";
  case GPU_FMT_RGBA8:
    return "RGBA8";
  case GPU_FMT_BGR8:
    return "BGR8";
  case GPU_FMT_BC1:
    return "BC1/DXT1";
  case GPU_FMT_BC2:
    return "BC2/DXT3";
  case GPU_FMT_BC3:
    return "BC3/DXT5";
  }
  return "unknown";
}

uint32_t gpu_texture_level_bytes(GpuFormat fmt, uint32_t w, uint32_t h) {
  uint32_t blocks = ((w + 3u) / 4u) * ((h + 3u) / 4u);
  switch (fmt) {
  case GPU_FMT_BGRA8:
  case GPU_FMT_RGBA8:
    return w * h * 4u;
  case GPU_FMT_BGR8:
    return w * h * 4u;
  case GPU_FMT_BC1:
    return blocks * 8u;
  case GPU_FMT_BC2:
  case GPU_FMT_BC3:
    return blocks * 16u;
  }
  return 0;
}

#ifdef X2_WITH_SDL
#include "../native/x2_log.h"
#include "gpu_internal.h"

SDL_GPUTextureFormat gpu_texture_sdl_format(GpuFormat f) {
  switch (f) {
  case GPU_FMT_BGRA8:
    return SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
  case GPU_FMT_RGBA8:
    return SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
  case GPU_FMT_BGR8:
    return SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
  case GPU_FMT_BC1:
    return SDL_GPU_TEXTUREFORMAT_BC1_RGBA_UNORM;
  case GPU_FMT_BC2:
    return SDL_GPU_TEXTUREFORMAT_BC2_RGBA_UNORM;
  case GPU_FMT_BC3:
    return SDL_GPU_TEXTUREFORMAT_BC3_RGBA_UNORM;
  }
  return SDL_GPU_TEXTUREFORMAT_INVALID;
}

static int g_format_support_report_requested;

void gpu_texture_request_format_support_report(void) {
  static const GpuFormat formats[] = {GPU_FMT_BGRA8, GPU_FMT_RGBA8,
                                      GPU_FMT_BGR8,  GPU_FMT_BC1,
                                      GPU_FMT_BC2,   GPU_FMT_BC3};
  unsigned int i;

  g_format_support_report_requested = 1;
  if (!g_gpu) {
    return;
  }
  g_format_support_report_requested = 0;
  for (i = 0; i < sizeof formats / sizeof formats[0]; ++i) {
    SDL_GPUTextureFormat format = gpu_texture_sdl_format(formats[i]);
    int supported =
        SDL_GPUTextureSupportsFormat(g_gpu, format, SDL_GPU_TEXTURETYPE_2D,
                                     SDL_GPU_TEXTUREUSAGE_SAMPLER)
            ? 1
            : 0;
    x2_log_error("gpu: texture format %s (%d) 2D sampler: %s\n",
                 gpu_texture_format_name(formats[i]), (int)format,
                 supported ? "supported" : "UNSUPPORTED");
  }
}

void gpu_texture_flush_format_support_report(void) {
  if (g_format_support_report_requested)
    gpu_texture_request_format_support_report();
}
#endif
