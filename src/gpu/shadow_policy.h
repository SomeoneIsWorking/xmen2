#ifndef X2_SHADOW_POLICY_H
#define X2_SHADOW_POLICY_H

#include "gpu_draw.h"

enum { GPU_SHADOW_NONE = 0, GPU_SHADOW_CASTER = 1, GPU_SHADOW_RECEIVER = 2 };

typedef struct {
  float light_view_projection[16];
  float inverse_view_projection[16];
  float light_direction[3];
} GpuShadowFramePolicy;

/* Enhancement policy, deliberately independent of SDL resource mechanics. */
unsigned gpu_shadow_draw_roles(const GpuDraw *draw);
int gpu_shadow_frame_policy(const GpuDraw *draw, GpuShadowFramePolicy *out);
void gpu_shadow_draw_matrix(const GpuShadowFramePolicy *frame,
                            const GpuDraw *draw, float out[16]);

#endif
