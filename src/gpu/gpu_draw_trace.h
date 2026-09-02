#ifndef X2_GPU_DRAW_TRACE_H
#define X2_GPU_DRAW_TRACE_H

#include "gpu_draw.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Draw-attribution diagnostics. Returns zero only when X2_DRAW_RANGE asks the
   renderer to omit this draw; omission is diagnostic, never a refusal. */
int gpu_draw_trace_consider(const GpuDraw *draw, unsigned long frame);
unsigned long gpu_draw_trace_draws_so_far(void);
void gpu_draw_trace_report(void);

/* Arms the existing bounded busy-frame diagnostic after process startup. This
   is for platform debug bridges whose configuration arrives after exec. */
void gpu_draw_trace_arm_busy_frame(unsigned long minimum_draws);
void gpu_draw_trace_disarm_frame_dump(void);

#ifdef __cplusplus
}
#endif

#endif
