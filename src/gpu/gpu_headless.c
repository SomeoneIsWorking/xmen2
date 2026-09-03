/*
 * Headless output: the frame renders into an off-screen texture and a harness
 * reads it back.
 *
 * The three answers a caller must be able to tell apart are "this run is not
 * headless", "the target exists but nothing has been drawn into it yet", and
 * "here are the pixels". Reading an untouched texture would photograph
 * uninitialised memory and look like a rendered frame, so the frame counter is
 * part of the contract rather than a diagnostic.
 */
#include "gpu_headless.h"

#include "gpu_device.h"
#include "gpu_internal.h"

#include <stdio.h>
#include <string.h>

/* Headless: no window, and the frame renders into this instead. */
static int g_headless;
static uint32_t g_headless_w = 800, g_headless_h = 600;
static int g_headless_size_explicit;
static SDL_GPUTexture *g_headless_tex;
static unsigned long g_headless_frames;

void gpu_device_headless(int on, uint32_t w, uint32_t h) {
  g_headless = on;
  if (w && h) {
    g_headless_w = w;
    g_headless_h = h;
    g_headless_size_explicit = 1;
  }
  if (on)
    printf("gpu: HEADLESS -- frames render into an off-screen %ux%u target; "
           "there is no window and nothing is presented to a screen.\n",
           g_headless_w, g_headless_h);
}

#ifdef X2_WITH_SDL
SDL_GPUTexture *gpu_headless_target(void) {
  SDL_GPUTextureCreateInfo ci;
  if (g_headless_tex)
    return g_headless_tex;
  memset(&ci, 0, sizeof ci);
  ci.type = SDL_GPU_TEXTURETYPE_2D;
  ci.format = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
  ci.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
  ci.width = g_headless_w;
  ci.height = g_headless_h;
  ci.layer_count_or_depth = 1;
  ci.num_levels = 1;
  g_headless_tex = SDL_CreateGPUTexture(g_gpu, &ci);
  if (!g_headless_tex)
    fprintf(stderr,
            "gpu: the headless target (%ux%u) could not be made: "
            "%s -- this run will draw NOTHING.\n",
            g_headless_w, g_headless_h, SDL_GetError());
  return g_headless_tex;
}
#endif

int gpu_headless_active(void) { return g_headless; }
uint32_t gpu_headless_width(void) { return g_headless_w; }
uint32_t gpu_headless_height(void) { return g_headless_h; }
unsigned long gpu_headless_frames(void) { return g_headless_frames; }
void gpu_headless_note_frame(void) { g_headless_frames++; }

void gpu_headless_follow_backbuffer(uint32_t width, uint32_t height) {
#ifdef X2_WITH_SDL
  if (!g_headless || g_headless_size_explicit)
    return;
  if (g_headless_tex) {
    SDL_ReleaseGPUTexture(g_gpu, g_headless_tex);
    g_headless_tex = NULL;
  }
  g_headless_w = width;
  g_headless_h = height;
#else
  (void)width;
  (void)height;
#endif
}

/*
 * How big a readback would be, and whether one is possible at all.
 *
 * Separate from the read because the caller has to allocate before it can ask,
 * and folding the query into the read as "pass a null buffer" collides with
 * the buffer-size check -- the caller then cannot tell "too small" from "not
 * headless" from "no frame yet", which are three different answers.
 */
int gpu_device_headless_size(uint32_t *w_out, uint32_t *h_out, char *why,
                             int whyn) {
  if (w_out)
    *w_out = 0;
  if (h_out)
    *h_out = 0;
#ifndef X2_WITH_SDL
  snprintf(why, (size_t)whyn,
           "this build has no SDL, so there is no "
           "renderer to read a frame from.");
  return 0;
#else
  if (!g_headless || !g_headless_tex) {
    snprintf(why, (size_t)whyn,
             "this run is not headless, so the frame goes to the screen "
             "and there is no off-screen target to read. Launch with "
             "--no-window.");
    return 0;
  }
  if (!g_headless_frames) {
    snprintf(why, (size_t)whyn,
             "the %ux%u headless target exists but NO frame has been "
             "rendered into it yet -- reading it would photograph an "
             "uninitialised texture. The run is still starting up.",
             g_headless_w, g_headless_h);
    return 0;
  }
  if (w_out)
    *w_out = g_headless_w;
  if (h_out)
    *h_out = g_headless_h;
  return 1;
#endif
}

int gpu_device_headless_read(void *bgra_out, uint32_t bytes, uint32_t *w_out,
                             uint32_t *h_out) {
#ifndef X2_WITH_SDL
  (void)bgra_out;
  (void)bytes;
  (void)w_out;
  (void)h_out;
  fprintf(stderr, "gpu: no SDL in this build, so there is nothing to read.\n");
  return 0;
#else
  SDL_GPUTransferBufferCreateInfo tci;
  SDL_GPUTransferBuffer *tb;
  SDL_GPUCommandBuffer *cmd;
  SDL_GPUCopyPass *cp;
  SDL_GPUTextureRegion src;
  SDL_GPUTextureTransferInfo dst;
  SDL_GPUFence *fence;
  uint32_t need = g_headless_w * g_headless_h * 4u;
  void *p;

  if (!g_headless || !g_headless_tex) {
    fprintf(stderr, "gpu: this run is not headless (or no frame has been "
                    "rendered), so there is no target to read.\n");
    return 0;
  }
  if (!g_headless_frames) {
    fprintf(stderr, "gpu: the headless target exists but NO frame has been "
                    "rendered into it -- reading it would photograph an "
                    "uninitialised texture.\n");
    return 0;
  }
  if (bytes < need) {
    fprintf(stderr, "gpu: the readback needs %u bytes, was given %u.\n", need,
            bytes);
    return 0;
  }
  /* Whatever is in flight has to have executed before it can be read. */
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
  memset(&tci, 0, sizeof tci);
  tci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
  tci.size = need;
  tb = SDL_CreateGPUTransferBuffer(g_gpu, &tci);
  if (!tb) {
    fprintf(stderr, "gpu: %s\n", SDL_GetError());
    return 0;
  }
  cmd = SDL_AcquireGPUCommandBuffer(g_gpu);
  cp = SDL_BeginGPUCopyPass(cmd);
  memset(&src, 0, sizeof src);
  memset(&dst, 0, sizeof dst);
  src.texture = g_headless_tex;
  src.w = g_headless_w;
  src.h = g_headless_h;
  src.d = 1;
  dst.transfer_buffer = tb;
  SDL_DownloadFromGPUTexture(cp, &src, &dst);
  SDL_EndGPUCopyPass(cp);
  fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
  if (fence) {
    SDL_WaitForGPUFences(g_gpu, true, &fence, 1);
    SDL_ReleaseGPUFence(g_gpu, fence);
  }
  p = SDL_MapGPUTransferBuffer(g_gpu, tb, false);
  if (!p) {
    fprintf(stderr, "gpu: mapping the readback failed: %s\n", SDL_GetError());
    SDL_ReleaseGPUTransferBuffer(g_gpu, tb);
    return 0;
  }
  memcpy(bgra_out, p, need);
  SDL_UnmapGPUTransferBuffer(g_gpu, tb);
  SDL_ReleaseGPUTransferBuffer(g_gpu, tb);
  if (w_out)
    *w_out = g_headless_w;
  if (h_out)
    *h_out = g_headless_h;
  return 1;
#endif
}
