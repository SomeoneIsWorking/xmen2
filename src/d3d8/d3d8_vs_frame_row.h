#pragma once

#include "d3d8_drawcall.h"
#include "d3d8_state.h"
#include "gpu_draw.h"

namespace x2::d3d8 {

/* The frame table's row for a programmable draw: its program run on the CPU
   executor over the draw's vertex range, reporting where it lands and the
   diffuse colour it hands the pixel stage. */
void d3d8_vs_frame_row(unsigned long draw, const D3D8State *s,
                       const D3D8DrawRequest *req, const GpuDraw *out);

} // namespace x2::d3d8
