/* gpu_offscreen.c -- the off-screen target the self-tests draw into; see
   gpu_draw.h. */
#include "../native/x2_log.h"
#include "gpu_draw.h"

#ifndef X2_WITH_SDL

static int no_sdl(const char *what) {
  x2_log_error("gpu: %s -- this build has no SDL, so there is no GPU.\n", what);
  return 0;
}
int gpu_offscreen_begin(uint32_t w, uint32_t h, float r, float g, float b,
                        float a) {
  (void)w;
  (void)h;
  (void)r;
  (void)g;
  (void)b;
  (void)a;
  return no_sdl("offscreen begin");
}
int gpu_offscreen_next_no_clear(void) { return no_sdl("offscreen next frame"); }
int gpu_offscreen_read(void *o, uint32_t n) {
  (void)o;
  (void)n;
  return no_sdl("offscreen read");
}
void gpu_offscreen_end(void) {}

#else /* X2_WITH_SDL */

#include "gpu_device.h"
#include "gpu_internal.h"
#include "gpu_readback.h"
#include "gpu_shadow.h"
#include "gpu_upload_batch.h"

#include <string.h>

static SDL_GPUTexture *g_off_tex;
static uint32_t g_off_w, g_off_h;

int gpu_offscreen_begin(uint32_t w, uint32_t h, float r, float g, float b,
                        float a) {
  SDL_GPUTextureCreateInfo ci;

  if (!g_gpu) {
    x2_log_error("gpu: no device.\n");
    return 0;
  }
  gpu_offscreen_end();

  memset(&ci, 0, sizeof ci);
  ci.type = SDL_GPU_TEXTURETYPE_2D;
  ci.format = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
  ci.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
  ci.width = w;
  ci.height = h;
  ci.layer_count_or_depth = 1;
  ci.num_levels = 1;
  g_off_tex = SDL_CreateGPUTexture(g_gpu, &ci);
  if (!g_off_tex) {
    x2_log_error("gpu: the off-screen target could not be made: %s\n",
                 SDL_GetError());
    return 0;
  }
  g_off_w = w;
  g_off_h = h;
  if (!gpu_frame_command_acquire()) {
    x2_log_error("gpu: no command buffer: %s\n", SDL_GetError());
    return 0;
  }
  gpu_set_offscreen_target(g_off_tex, w, h);
  gpu_shadow_frame_begin();
  gpu_frame_clear(1u, r, g, b, a, 1.0f, 0);
  return 1;
}

int gpu_offscreen_next_no_clear(void) {
  if (!g_gpu || !g_off_tex) {
    x2_log_error("gpu: no off-screen target to continue.\n");
    return 0;
  }
  /* Execute even a clear-only first frame before replacing its command
     buffer. Queue submission order then makes it the known previous image
     for the frame that follows. */
  gpu_pass_begin();
  gpu_upload_batch_flush(g_gpu);
  gpu_shadow_frame_submit();
  if (g_pass) {
    SDL_EndGPURenderPass(g_pass);
    g_pass = NULL;
  }
  if (g_cmd) {
    SDL_SubmitGPUCommandBuffer(g_cmd);
    g_cmd = NULL;
  }

  if (!gpu_frame_command_acquire()) {
    x2_log_error("gpu: no command buffer for the next off-screen "
                 "frame: %s\n",
                 SDL_GetError());
    return 0;
  }
  gpu_set_offscreen_target(g_off_tex, g_off_w, g_off_h);
  gpu_shadow_frame_begin();
  /* gpu_frame_begin resets this mask on the real path. This helper owns an
     already-created target, so reproduce that boundary explicitly. */
  gpu_frame_clear(0u, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0u);
  return 1;
}

int gpu_offscreen_read(void *out, uint32_t bytes) {
  uint32_t need = g_off_w * g_off_h * 4u;
  SDL_GPUFence *fence;

  if (!g_off_tex) {
    x2_log_error("gpu: no off-screen target.\n");
    return 0;
  }
  if (bytes < need) {
    x2_log_error("gpu: the readback needs %u bytes, was given %u.\n", need,
                 bytes);
    return 0;
  }
  /* The draws have to have executed before they can be read back. */
  gpu_upload_batch_flush(g_gpu);
  gpu_shadow_frame_submit();
  if (g_pass) {
    SDL_EndGPURenderPass(g_pass);
    g_pass = NULL;
  }
  if (g_cmd) {
    fence = SDL_SubmitGPUCommandBufferAndAcquireFence(g_cmd);
    g_cmd = NULL;
    if (fence) {
      SDL_WaitForGPUFences(g_gpu, true, &fence, 1);
      SDL_ReleaseGPUFence(g_gpu, fence);
    }
  }
  return gpu_readback_texture_rgba(g_gpu, g_off_tex, g_off_w, g_off_h, out,
                                   bytes);
}

void gpu_offscreen_end(void) {
  gpu_upload_batch_flush(g_gpu);
  gpu_shadow_frame_submit();
  if (g_pass) {
    SDL_EndGPURenderPass(g_pass);
    g_pass = NULL;
  }
  if (g_cmd) {
    SDL_SubmitGPUCommandBuffer(g_cmd);
    g_cmd = NULL;
  }
  if (g_off_tex) {
    SDL_ReleaseGPUTexture(g_gpu, g_off_tex);
    g_off_tex = NULL;
  }
  gpu_set_offscreen_target(NULL, 0, 0);
  g_swap = NULL;
}

#endif /* X2_WITH_SDL */
