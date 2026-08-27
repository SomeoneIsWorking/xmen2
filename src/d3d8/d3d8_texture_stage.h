#ifndef X2_D3D8_TEXTURE_STAGE_H
#define X2_D3D8_TEXTURE_STAGE_H

#include "d3d8_state.h"
#include "gpu_draw.h"

int d3d8_texture_arg(uint32_t value, const char *what);

/* Resolve one sticky guest texture-stage binding to its GPU resource while
   retaining the live denominator for bound-but-unresolved stages. */
GpuTexture d3d8_texture_stage_resolve(unsigned stage, uint32_t guest);
void d3d8_texture_stage_unresolved(unsigned long *count);

/* Lower the evidenced Dead Zone stage-1 material. Returns zero, with the
   unsupported state named, instead of drawing a plausible approximation. */
int d3d8_texture_stage1_lower(const D3D8State *state, GpuTexture texture,
                              GpuDraw *draw);

#endif
