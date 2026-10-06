#ifndef X2_GPU_SHADOW_H
#define X2_GPU_SHADOW_H

#include "gpu_draw.h"
#include "shadow_policy.h"

#include <stdint.h>

/* The shadow tail of the fragment uniforms; layout matches d3d8_fixed.frag. */
typedef struct {
  uint32_t enabled;
  float darkness;
  uint32_t pad[2];
  /* World position to camera view depth. */
  float depth_plane[4];
  /* xyz: the light's travel direction; w: one tile texel in tile UV. */
  float light[4];
  /* Filter tap spacing per cascade, in tile UV. */
  float kernel_step[4];
  /* Per cascade: split far, blend start, depth bias, normal offset. */
  float cascade[GPU_SHADOW_CASCADES][4];
  float view_projection[GPU_SHADOW_CASCADES][16];
} GpuShadowPixelBlock;

typedef struct {
  int enabled;
  /* Clip to world, for programmable receivers whose VS output is clip space. */
  float world_from_clip[16];
  GpuShadowPixelBlock pixel;
} GpuShadowSample;

struct SDL_GPUBuffer;
struct SDL_GPUSampler;
struct SDL_GPUTexture;

void gpu_shadow_configure(int enabled, uint32_t resolution);
void gpu_shadow_frame_begin(void);
void gpu_shadow_record(const GpuDraw *draw, struct SDL_GPUBuffer *vertices,
                       uint64_t vertex_serial, struct SDL_GPUBuffer *indices,
                       uint32_t first_index, struct SDL_GPUTexture *texture,
                       struct SDL_GPUSampler *sampler, uint32_t index_count);
void gpu_shadow_frame_submit(void);
int gpu_shadow_sample(const GpuDraw *draw, GpuShadowSample *sample);
#ifdef X2_WITH_SDL
#include <SDL3/SDL.h>
SDL_GPUTextureSamplerBinding gpu_shadow_binding(int enabled);
#endif
void gpu_shadow_report(void);
void gpu_shadow_shutdown(void);

#endif
