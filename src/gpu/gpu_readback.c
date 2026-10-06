/* See gpu_readback.h. */
#include "gpu_readback.h"

#include "../native/x2_log.h"

#include <string.h>

int gpu_readback_texture_rgba(SDL_GPUDevice *device, SDL_GPUTexture *texture,
                              uint32_t width, uint32_t height, void *out,
                              uint32_t out_bytes) {
  SDL_GPUTransferBufferCreateInfo tci;
  SDL_GPUCommandBuffer *cmd;
  SDL_GPUTextureRegion src;
  SDL_GPUTextureTransferInfo dst;
  SDL_GPUTransferBuffer *tb;
  SDL_GPUCopyPass *cp;
  SDL_GPUFence *fence;
  uint32_t need;
  void *p;

  if (!device || !texture || !out) {
    x2_log_error("gpu readback: called without a device, texture, or "
                 "destination.\n");
    return 0;
  }
  if (!width || height > UINT32_MAX / 4u / width)
    return 0;
  need = width * height * 4u;
  if (out_bytes < need) {
    x2_log_error("gpu readback: the readback needs %u bytes, was given %u.\n",
                 need, out_bytes);
    return 0;
  }
  memset(&tci, 0, sizeof tci);
  tci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
  tci.size = need;
  tb = SDL_CreateGPUTransferBuffer(device, &tci);
  if (!tb) {
    x2_log_error("gpu readback: %s\n", SDL_GetError());
    return 0;
  }
  cmd = SDL_AcquireGPUCommandBuffer(device);
  if (!cmd) {
    x2_log_error("gpu readback: no command buffer: %s\n", SDL_GetError());
    SDL_ReleaseGPUTransferBuffer(device, tb);
    return 0;
  }
  cp = SDL_BeginGPUCopyPass(cmd);
  memset(&src, 0, sizeof src);
  memset(&dst, 0, sizeof dst);
  src.texture = texture;
  src.w = width;
  src.h = height;
  src.d = 1;
  dst.transfer_buffer = tb;
  SDL_DownloadFromGPUTexture(cp, &src, &dst);
  SDL_EndGPUCopyPass(cp);
  fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
  if (fence) {
    SDL_WaitForGPUFences(device, true, &fence, 1);
    SDL_ReleaseGPUFence(device, fence);
  }
  p = SDL_MapGPUTransferBuffer(device, tb, false);
  if (!p) {
    x2_log_error("gpu readback: mapping the readback failed: %s\n",
                 SDL_GetError());
    SDL_ReleaseGPUTransferBuffer(device, tb);
    return 0;
  }
  memcpy(out, p, need);
  SDL_UnmapGPUTransferBuffer(device, tb);
  SDL_ReleaseGPUTransferBuffer(device, tb);
  return 1;
}
