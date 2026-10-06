#ifndef D3D8_LIGHTING_H
#define D3D8_LIGHTING_H

/*
 * D3DRS_LIGHTING: the material, the light table, and what a draw is lit by.
 *
 * Split from d3d8_drawcall.c, which translates the rest of the state machine.
 * Lighting is its own concern because it has its own layout knowledge
 * (D3DMATERIAL8 is 17 floats, D3DLIGHT8 26 dwords, both read by offset in ONE
 * place), its own diagnostics, and its own survey of what the engine actually
 * asked for.
 */
#include "d3d8_state.h"

#include "gpu_draw.h"

#include <stdint.h>

/* Fill the draw's material, ambient and light table from the device state. */
void d3d8_fill_lighting(const D3D8State *s, GpuDraw *out);

/* D3DCOLOR (ARGB dword) to the shader's RGBA floats. */
void d3d8_argb_to_rgba(uint32_t c, float *out);

/* Everything the lighting diagnostics measured, for the run report. */
void d3d8_lighting_report(void);

#endif /* D3D8_LIGHTING_H */
