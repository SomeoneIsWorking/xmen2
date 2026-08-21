/* Reusable SDL_GPU staging storage for buffer and texture uploads. */
#ifndef GPU_UPLOAD_H
#define GPU_UPLOAD_H

#include <stdint.h>

struct SDL_GPUDevice;
struct SDL_GPUTransferBuffer;

typedef struct {
    struct SDL_GPUTransferBuffer *buffer;
    uint32_t capacity;
} GpuUploadStaging;

/*
 * Copy bytes into staging storage whose allocation is retained by the owning
 * GPU resource. SDL's cycle operation preserves pending uploads without
 * forcing callers to allocate and retire a transfer-buffer object per copy.
 * `created` is set only when this call had to create the retained allocation.
 */
struct SDL_GPUTransferBuffer *gpu_upload_stage(
    struct SDL_GPUDevice *device, GpuUploadStaging *staging,
    uint32_t capacity, const void *data, uint32_t bytes, int *created);

void gpu_upload_staging_destroy(struct SDL_GPUDevice *device,
                                GpuUploadStaging *staging);

#endif
