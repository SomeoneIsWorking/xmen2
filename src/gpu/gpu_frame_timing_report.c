#include "gpu_frame_timing_report.h"
#include "../native/x2_log.h"

#include "gpu_device.h"
#include "gpu_draw.h"
#include "gpu_frame_timing.h"
#include "gpu_internal.h"

#include <stdio.h>

static void slow_frame_report(unsigned long frame, unsigned long long dt_ns) {
  unsigned long long draw_ns, upload_ns;
  gpu_frame_host_share(&draw_ns, &upload_ns);
  x2_log_error("gpu: frame %lu took %.0f ms; host draw %.1f ms + "
               "upload %.1f ms, the rest is guest logic and the submit\n",
               frame, (double)dt_ns * 1e-6, (double)draw_ns * 1e-6,
               (double)upload_ns * 1e-6);
}

void gpu_frame_timing_report_install(void) {
  gpu_frame_timing_slow_hook = slow_frame_report;
}

void gpu_frame_timing_report_interval(void) {
  /*
   * Frame-phase profiling, live.
   *
   * Two reads, one line: the DEVICE's present-to-present wall time
   * and the DRAW side's host share (gpu_draw + uploads). The line
   * the frame is paced to 60 fps at reads zero draw time and the
   * wall time is the vsync wait; an UNPACED gameplay run reads the
   * frame cost and the host's share of it, which is where a hotspot
   * has to show up before anything gets "fixed". Printed at zero as
   * a baseline like everything else here, not only once non-zero.
   */
  unsigned long long fns, fmin, fmax, esub;
  const unsigned long *hist;
  unsigned long long dns, uns, una, unsb, tc, swns;
  unsigned long up, sb, intervals, swn;
  gpu_device_perf(&fns, &fmin, &fmax, &esub, &intervals, &hist);
  gpu_frame_timing_swapchain_wait(&swns, &swn);
  gpu_draw_perf(&dns, &uns, &una, &unsb, &tc, &up, &sb);
  x2_log_error("[HB]           perf: frame wall avg %.1f ms "
               "min %.1f max %.1f (of %lu intervals) -- host "
               "draw %.2f ms/frame, host upload %.2f "
               "ms/frame (alloc %.2f + record %.2f), %lu "
               "uploads and %lu transfer-buffer alloc(s) batched into %lu copy "
               "command buffer(s), swapchain wait %.2f ms/frame over %lu "
               "acquisition(s)\n",
               fns && intervals ? (double)fns * 1e-6 / (double)intervals : 0.0,
               fmin ? (double)fmin * 1e-6 : 0.0, (double)fmax * 1e-6, intervals,
               intervals ? (double)dns * 1e-6 / (double)intervals : 0.0,
               intervals ? (double)uns * 1e-6 / (double)intervals : 0.0,
               intervals ? (double)una * 1e-6 / (double)intervals : 0.0,
               intervals ? (double)unsb * 1e-6 / (double)intervals : 0.0,
               (unsigned long)up, (unsigned long)tc, sb,
               swn ? (double)swns * 1e-6 / (double)swn : 0.0, swn);
  (void)esub;
  (void)hist;
}
