#ifndef D3D8_LIGHT_DUMP_H
#define D3D8_LIGHT_DUMP_H

/* X2_LIGHT_DUMP -- see d3d8_light_dump.c. */
#include "gpu_draw.h"

#include <stdint.h>

/* Called with the finished draw, once its lighting is filled in. */
void d3d8_light_dump(const GpuDraw *d);

/* What the fill path saw, for the dump to print alongside the draw. */
void d3d8_light_note_ambient(uint32_t raw);
void d3d8_light_note_source(int slot, int d3d_index);
void d3d8_light_note_viewpos(float x, float y, float z);

/*
 * Which D3D light INDEX the packed light in `slot` came from, or -1.
 *
 * The packed table is the ENABLED lights in order, so "light 0" in a dump is
 * not the index the engine set -- and without the index a black light cannot
 * be traced back to the SetLight that made it.
 */
int d3d8_light_source_index(int slot);

/* Always printed, including when the dump fired zero times. */
void d3d8_light_dump_report(void);

#endif /* D3D8_LIGHT_DUMP_H */
