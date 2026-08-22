#ifndef X2_GPU_DRAW_TRACE_H
#define X2_GPU_DRAW_TRACE_H

#include "gpu_draw.h"

/* Draw-attribution diagnostics. Returns zero only when X2_DRAW_RANGE asks the
   renderer to omit this draw; omission is diagnostic, never a refusal. */
int gpu_draw_trace_consider(const GpuDraw *draw, unsigned long frame);
unsigned long gpu_draw_trace_draws_so_far(void);
void gpu_draw_trace_report(void);

#endif
