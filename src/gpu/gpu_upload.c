#include "../native/x2_log.h"
/* See gpu_upload.h. */
#include "gpu_upload.h"

#include <SDL3/SDL.h>

#include <stdio.h>
#include <string.h>

SDL_GPUTransferBuffer *gpu_upload_stage(SDL_GPUDevice *device,
                                        GpuUploadStaging *staging,
                                        uint32_t capacity, const void *data,
                                        uint32_t bytes, int *created) {
  SDL_GPUTransferBufferCreateInfo ci;
  void *mapped;

  if (created)
    *created = 0;
  if (!device || !staging || !data || !bytes || bytes > capacity) {
    x2_log_error("gpu: invalid upload staging request: %u byte(s) "
                 "into a %u-byte allocation.\n",
                 bytes, capacity);
    return NULL;
  }
  if (!staging->buffer) {
    memset(&ci, 0, sizeof ci);
    ci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    ci.size = capacity;
    staging->buffer = SDL_CreateGPUTransferBuffer(device, &ci);
    if (!staging->buffer) {
      x2_log_error("gpu: transfer buffer (%u bytes) failed: %s\n", capacity,
                   SDL_GetError());
      return NULL;
    }
    staging->capacity = capacity;
    if (created)
      *created = 1;
  }
  if (capacity > staging->capacity || bytes > staging->capacity) {
    x2_log_error("gpu: retained upload staging is %u bytes but the "
                 "resource now requires %u bytes. Refusing a resource "
                 "whose size changed after creation.\n",
                 staging->capacity, capacity);
    return NULL;
  }

  /*
   * The preceding upload may still be queued. Cycling is SDL_GPU's owned
   * ring-buffer operation: it selects unbound backing storage (or grows the
   * internal ring) without replacing this long-lived transfer-buffer object.
   */
  mapped = SDL_MapGPUTransferBuffer(device, staging->buffer, true);
  if (!mapped) {
    x2_log_error("gpu: mapping the transfer buffer failed: %s\n",
                 SDL_GetError());
    return NULL;
  }
  memcpy(mapped, data, bytes);
  SDL_UnmapGPUTransferBuffer(device, staging->buffer);
  return staging->buffer;
}

void gpu_upload_staging_destroy(SDL_GPUDevice *device,
                                GpuUploadStaging *staging) {
  if (!device || !staging || !staging->buffer)
    return;
  SDL_ReleaseGPUTransferBuffer(device, staging->buffer);
  staging->buffer = NULL;
  staging->capacity = 0;
}
