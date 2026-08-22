#ifndef X2_GPU_SHADOW_H
#define X2_GPU_SHADOW_H

#include "gpu_draw.h"

#include <stdint.h>

typedef struct {
    int enabled;
    float matrix[16];
    float texel_size[2];
    float depth_bias;
    float darkness;
} GpuShadowSample;

struct SDL_GPUBuffer;
struct SDL_GPUSampler;
struct SDL_GPUTexture;

void gpu_shadow_configure(int enabled, uint32_t resolution);
void gpu_shadow_frame_begin(void);
void gpu_shadow_record(const GpuDraw *draw,
                       struct SDL_GPUBuffer *vertices,
                       struct SDL_GPUBuffer *indices,
                       struct SDL_GPUTexture *texture,
                       struct SDL_GPUSampler *sampler,
                       uint32_t index_count);
void gpu_shadow_frame_submit(void);
int gpu_shadow_sample(const GpuDraw *draw, GpuShadowSample *sample);
struct SDL_GPUTexture *gpu_shadow_texture(void);
struct SDL_GPUSampler *gpu_shadow_sampler(void);
void gpu_shadow_report(void);
void gpu_shadow_shutdown(void);

#endif
