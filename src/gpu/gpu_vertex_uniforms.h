#ifndef GPU_VERTEX_UNIFORMS_H
#define GPU_VERTEX_UNIFORMS_H

/*
 * The vertex stage's uniforms for one draw.
 *
 * Split from gpu_draw.c, which decides whether and how a draw is recorded:
 * this owns the VertexState block's C layout, which must match
 * src/gpu/shaders/d3d8_vertex_stage.glsl field for field, and fills it from
 * the draw. A draw the GPU runs a VS 1.1 program for also gets that
 * program's two blocks (gpu_vs_program.h).
 */
#ifdef X2_WITH_SDL
#include "gpu_draw.h"
#include "gpu_shadow.h"

#include <SDL3/SDL.h>

/* Fill the vertex uniforms from `d` and `shadow` and push them into slot 0,
   and the program's blocks into slots 1 and 2 when `d` has one. */
void gpu_vertex_uniforms_push(SDL_GPUCommandBuffer *command, const GpuDraw *d,
                              const GpuShadowSample *shadow);
#endif

#endif /* GPU_VERTEX_UNIFORMS_H */
