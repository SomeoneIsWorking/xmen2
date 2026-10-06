/* gpu_pass_attachments.c -- see gpu_pass_attachments.h. */
#include "gpu_pass_attachments.h"

#ifdef X2_WITH_SDL
#include <string.h>

void gpu_pass_color_target(SDL_GPUColorTargetInfo *ct, SDL_GPUTexture *target,
                           const GpuPassClear *clear, int reopen) {
  memset(ct, 0, sizeof *ct);
  ct->texture = target;
  ct->clear_color.r = clear->r;
  ct->clear_color.g = clear->g;
  ct->clear_color.b = clear->b;
  ct->clear_color.a = clear->a;
  /*
   * A frame starts black when the engine does not clear colour itself.
   *
   * DONT_CARE is not black: tiled GPUs may expose recycled attachment
   * memory in every pixel the game does not overwrite. That appeared as
   * scene fragments in the unused edges and, on sparse loading frames, as
   * noise over nearly the whole window. LOAD is no better at frame start:
   * swapchain history is undefined and the logical presentation texture is
   * persistent. An opaque-black CLEAR gives D3D's discarded back buffer a
   * deterministic value without changing an explicit game clear.
   *
   * A MID-frame reopen is different. Pixels drawn before a late depth-only
   * clear are real current-frame contents and must be loaded.
   */
  if (clear->mask & 1u) {
    ct->load_op = SDL_GPU_LOADOP_CLEAR;
  } else if (reopen) {
    ct->load_op = SDL_GPU_LOADOP_LOAD;
  } else {
    ct->clear_color.r = 0.0f;
    ct->clear_color.g = 0.0f;
    ct->clear_color.b = 0.0f;
    ct->clear_color.a = 1.0f;
    ct->load_op = SDL_GPU_LOADOP_CLEAR;
  }
  ct->store_op = SDL_GPU_STOREOP_STORE;
}

void gpu_pass_depth_target(SDL_GPUDepthStencilTargetInfo *dt,
                           SDL_GPUTexture *depth, const GpuPassClear *clear,
                           int reopen) {
  memset(dt, 0, sizeof *dt);
  if (depth) {
    dt->texture = depth;
    /*
     * CLEAR when the engine asked, and CLEAR when it did not.
     *
     * The contents from the previous frame are meaningless to this one and
     * LOADing them would depth-test against the last frame's geometry.
     * D3D8's own semantics are that a frame that draws depth-tested
     * geometry clears Z first; a frame that forgets is drawing against
     * garbage on real hardware too, and 1.0 is the value that lets
     * everything through rather than a value chosen to hide the mistake.
     */
    dt->clear_depth = (clear->mask & 2u) ? clear->depth : 1.0f;
    dt->clear_stencil = (Uint8)((clear->mask & 4u) ? clear->stencil : 0u);
    /* Mid-frame, only what the engine ASKED to clear is cleared: a depth
       clear before the HUD must not throw away the colour, and a colour
       clear must not throw away the depth. */
    dt->load_op = (!reopen || (clear->mask & 2u)) ? SDL_GPU_LOADOP_CLEAR
                                                  : SDL_GPU_LOADOP_LOAD;
    dt->store_op = SDL_GPU_STOREOP_STORE;
    dt->stencil_load_op = (!reopen || (clear->mask & 4u)) ? SDL_GPU_LOADOP_CLEAR
                                                          : SDL_GPU_LOADOP_LOAD;
    dt->stencil_store_op = SDL_GPU_STOREOP_STORE;
  }
}
#endif
