#pragma once

/*
 * The frame render pass's attachments: what each clears, to what, and what
 * it keeps -- at the start of a frame and at a reopen in the middle of one.
 */
#include <cstdint>

namespace x2::gpu {

/* What the next render pass must clear with, as the engine's Clear asked:
   mask bit 0 colour, 1 depth, 2 stencil. */
struct GpuPassClear {
  unsigned mask;
  float r, g, b, a, depth;
  uint32_t stencil;
};

} // namespace x2::gpu

#ifdef X2_WITH_SDL
#include <SDL3/SDL.h>

namespace x2::gpu {

/* `reopen` is a pass started again mid-frame because a clear arrived after
   drawing began: what it does not clear, it keeps. */
void gpu_pass_color_target(SDL_GPUColorTargetInfo *ct, SDL_GPUTexture *target,
                           const GpuPassClear *clear, int reopen);

/* Leaves `dt` zeroed when there is no `depth` target. */
void gpu_pass_depth_target(SDL_GPUDepthStencilTargetInfo *dt,
                           SDL_GPUTexture *depth, const GpuPassClear *clear,
                           int reopen);

} // namespace x2::gpu
#endif
