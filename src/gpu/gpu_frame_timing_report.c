#include "gpu_frame_timing_report.h"

#include "gpu_frame_timing.h"
#include "gpu_internal.h"

#include <stdio.h>

static void slow_frame_report(unsigned long frame, unsigned long long dt_ns) {
  unsigned long long draw_ns, upload_ns;
  gpu_frame_host_share(&draw_ns, &upload_ns);
  fprintf(stderr,
          "gpu: frame %lu took %.0f ms; host draw %.1f ms + "
          "upload %.1f ms, the rest is guest logic and the submit\n",
          frame, (double)dt_ns * 1e-6, (double)draw_ns * 1e-6,
          (double)upload_ns * 1e-6);
}

void gpu_frame_timing_report_install(void) {
  gpu_frame_timing_slow_hook = slow_frame_report;
}
