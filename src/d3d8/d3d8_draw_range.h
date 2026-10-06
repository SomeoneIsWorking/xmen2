/*
 * d3d8_draw_range.h -- does a draw read outside the vertex stream bound to
 * it? See d3d8_draw_range.c for why the check exists and what it refuses.
 */
#ifndef D3D8_DRAW_RANGE_H
#define D3D8_DRAW_RANGE_H

#include "d3d8_drawcall.h"

#include <stdint.h>

/* 1 when every vertex the draw fetches lies inside its stream of
   `stride`-byte vertices; 0, counted and logged, when one does not or the
   indices cannot be read. */
int d3d8_draw_range_ok(const D3D8DrawRequest *req, uint32_t stride);

/* The largest of `count` indices from `first` in `indices` (32-bit when
   `is32`, else 16-bit). `serial` names the buffer's uploaded contents
   (gpu_buffer_serial): a range already scanned under the same serial is
   answered without reading the indices again. 0 is no serial, and always
   scans. */
uint32_t d3d8_index_max(uint64_t serial, const void *indices, int is32,
                        uint32_t first, uint32_t count);

void d3d8_draw_range_report(void);

#endif
