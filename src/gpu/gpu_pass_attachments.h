#ifndef GPU_PASS_ATTACHMENTS_H
#define GPU_PASS_ATTACHMENTS_H

/*
 * The frame render pass's attachments: what each clears, to what, and what
 * it keeps -- at the start of a frame and at a reopen in the middle of one.
 */
#include <stdint.h>

/* What the next render pass must clear with, as the engine's Clear asked:
   mask bit 0 colour, 1 depth, 2 stencil. */
typedef struct GpuPassClear {
  unsigned mask;
  float r, g, b, a, depth;
  uint32_t stencil;
} GpuPassClear;

#ifdef X2_WITH_SDL
#include <SDL3/SDL.h>

/* `reopen` is a pass started again mid-frame because a clear arrived after
   drawing began: what it does not clear, it keeps. */
void gpu_pass_color_target(SDL_GPUColorTargetInfo *ct, SDL_GPUTexture *target,
                           const GpuPassClear *clear, int reopen);

/* Leaves `dt` zeroed when there is no `depth` target. */
void gpu_pass_depth_target(SDL_GPUDepthStencilTargetInfo *dt,
                           SDL_GPUTexture *depth, const GpuPassClear *clear,
                           int reopen);
#endif

#endif /* GPU_PASS_ATTACHMENTS_H */
