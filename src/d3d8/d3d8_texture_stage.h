#ifndef X2_D3D8_TEXTURE_STAGE_H
#define X2_D3D8_TEXTURE_STAGE_H

#include "d3d8_state.h"
#include "gpu_draw.h"

int d3d8_texture_arg(uint32_t value, const char *what);

/* Lower the evidenced Dead Zone stage-1 material. Returns zero, with the
   unsupported state named, instead of drawing a plausible approximation. */
int d3d8_texture_stage1_lower(const D3D8State *state, GpuTexture texture,
                              GpuDraw *draw);

#endif
