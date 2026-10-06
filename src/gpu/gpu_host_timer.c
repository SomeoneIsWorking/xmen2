#include "gpu_host_timer.h"

#include "gpu_internal.h"

#include <lucent/cvar_c.h>

/* -1 until the cvar has been read: a frame can be set up before the runtime
   configuration is, and reading it once keeps the draw path to a load. */
static int g_armed = -1;
static GpuHostTimes g_total, g_frame;

int gpu_host_timer_armed(void) {
  if (g_armed < 0) {
    g_armed = lucent_cvar_flag("gpu.host_timing", 0) != 0;
  }
  return g_armed;
}

unsigned long long gpu_host_timer_ns(void) {
  return gpu_host_timer_armed() ? gpu_perf_now_ns() : 0ull;
}

void gpu_host_timer_draw(unsigned long long t0) {
  if (gpu_host_timer_armed()) {
    unsigned long long dt = gpu_perf_now_ns() - t0;
    g_total.draw_ns += dt;
    g_frame.draw_ns += dt;
  }
}

void gpu_host_timer_upload(unsigned long long t0, unsigned long long staged) {
  if (gpu_host_timer_armed()) {
    unsigned long long now = gpu_perf_now_ns();
    g_total.upload_alloc_ns += staged - t0;
    g_total.upload_record_ns += now - staged;
    g_total.upload_ns += now - t0;
    g_frame.upload_ns += now - t0;
  }
}

GpuHostTimes gpu_host_timer_totals(void) { return g_total; }

void gpu_host_timer_frame_reset(void) {
  g_frame.draw_ns = 0;
  g_frame.upload_ns = 0;
}

GpuHostTimes gpu_host_timer_frame(void) { return g_frame; }
