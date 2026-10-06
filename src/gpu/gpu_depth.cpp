/*
 * Which depth format this device actually has, and the target made from it.
 *
 * Its own owner because the answer is a property of the DRIVER, cached for the
 * device's lifetime, and every other module only ever asks. Keeping it beside
 * the device's lifetime left an unrelated piece of driver interrogation inside
 * the file that owns frames.
 */
#include "gpu_depth.h"

#include "gpu_internal.h"
#include "gpu_present.h"
#include "x2_log.h"

static SDL_GPUTextureFormat g_depth_fmt;

void gpu_depth_forget(void) { g_depth_fmt = SDL_GPU_TEXTUREFORMAT_INVALID; }

SDL_GPUTextureFormat gpu_depth_format(void) {
  static const SDL_GPUTextureFormat WANT[] = {
      SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT,
      SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT,
      SDL_GPU_TEXTUREFORMAT_D24_UNORM,
      SDL_GPU_TEXTUREFORMAT_D32_FLOAT,
      SDL_GPU_TEXTUREFORMAT_D16_UNORM,
  };
  unsigned i;

  if (g_depth_fmt != SDL_GPU_TEXTUREFORMAT_INVALID)
    return g_depth_fmt;
  if (!g_gpu)
    return SDL_GPU_TEXTUREFORMAT_INVALID;
  for (i = 0; i < sizeof WANT / sizeof WANT[0]; i++) {
    if (SDL_GPUTextureSupportsFormat(
            g_gpu, WANT[i], SDL_GPU_TEXTURETYPE_2D,
            SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET)) {
      g_depth_fmt = WANT[i];
      return g_depth_fmt;
    }
  }
  {
    static int told;
    if (!told++)
      x2_log_error("gpu: this device supports NONE of the five depth "
                   "formats asked for, so there is no depth buffer "
                   "and everything draws in submission order.\n");
  }
  return SDL_GPU_TEXTUREFORMAT_INVALID;
}

SDL_GPUTexture *gpu_depth_target(uint32_t w, uint32_t h) {
  if (!g_gpu || !w || !h)
    return NULL;
  if (gpu_depth_format() == SDL_GPU_TEXTUREFORMAT_INVALID)
    return NULL;
  return gpu_present_depth_target(g_gpu, w, h, g_depth_fmt);
}
