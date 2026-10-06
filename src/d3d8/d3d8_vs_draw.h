#ifndef D3D8_VS_DRAW_H
#define D3D8_VS_DRAW_H

/*
 * Where a VS 1.1 draw's vertices come from.
 *
 * The GPU runs the guest's program when it can (issue #187): the draw binds
 * the guest's own vertex buffer and carries the packed program and the
 * device's constant file. A program the GPU has no form for runs on the CPU
 * executor instead, into a buffer made for that draw. Split from
 * d3d8_drawcall.c, which builds the rest of the draw either way.
 */
#include "d3d8_drawcall.h"
#include "d3d8_state.h"
#include "gpu_draw.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Fill `out`'s vertex source and layout for the programmable draw `req`.
   0, with the reason logged, refuses the draw. */
int d3d8_vs_draw_source(const D3D8State *s, const D3D8DrawRequest *req,
                        GpuDraw *out);

typedef enum { D3D8_VS_ON_GPU, D3D8_VS_ON_CPU } D3D8VsExecutor;

/* The same on the executor named, which the differential selftest
   (d3d8_vs_gpu_selftest.c) uses to draw one program both ways. On the GPU it
   refuses a program with no GPU form. */
int d3d8_vs_draw_source_on(const D3D8State *s, const D3D8DrawRequest *req,
                           D3D8VsExecutor executor, GpuDraw *out);

/* Programmable draws the GPU ran, and the vertices their buffers held. */
void d3d8_vs_draw_gpu_counts(unsigned long *draws, unsigned long *vertices);

#ifdef __cplusplus
}
#endif

#endif /* D3D8_VS_DRAW_H */
