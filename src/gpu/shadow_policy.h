#pragma once

#include "gpu_draw.h"

#include <cstdint>

namespace x2::gpu {

inline constexpr int GPU_SHADOW_NONE = 0;
inline constexpr int GPU_SHADOW_CASTER = 1;
inline constexpr int GPU_SHADOW_RECEIVER = 2;

inline constexpr int GPU_SHADOW_CASCADES = 4;

struct GpuShadowCascade {
  float light_view_projection[16];
  /* Camera view depths: where the slice starts, where the blend into the next
   * cascade begins, and where this cascade ends. */
  float slice_near, blend_start, split_far;
  /* World units the map spans across and up, one texel's size, and the world
   * depth the light clip box covers. */
  float extent[2];
  float texel_world;
  float depth_range;
  /* Receiver terms: NDC depth bias, world normal offset at grazing light and
   * the filter tap spacing in texels. */
  float depth_bias, normal_offset, kernel_texels;
};

struct GpuShadowFramePolicy {
  GpuShadowCascade cascade[GPU_SHADOW_CASCADES];
  float inverse_view_projection[16];
  float light_direction[3];
  /* World position to camera view depth: dot(vec4(position, 1), plane). */
  float view_depth_plane[4];
  float view_near, view_far;
};

/* Enhancement policy, deliberately independent of SDL resource mechanics. */
unsigned gpu_shadow_draw_roles(const GpuDraw *draw);
/* Fits the frame's cascades from the draw's camera; `resolution` is one
 * cascade tile's side in texels. */
int gpu_shadow_frame_policy(const GpuDraw *draw, uint32_t resolution,
                            GpuShadowFramePolicy *out);
void gpu_shadow_draw_matrix(const GpuShadowFramePolicy *frame, unsigned cascade,
                            const GpuDraw *draw, float out[16]);

} // namespace x2::gpu
