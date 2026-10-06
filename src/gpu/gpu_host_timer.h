/*
 * gpu_host_timer.h -- the host's draw and upload time, armed by
 * `gpu.host_timing` (off by default).
 *
 * WHAT IT MEASURES. Wall time of the HOST's share of a frame only: draw
 * submission and transfer-buffer uploads. The guest runs on the same thread,
 * so a frame's wall time is guest crossings plus these paths plus the submit
 * at gpu_frame_end, and the guest share is what is left over. The per-frame
 * share attributes a slow frame at the moment it ends; the totals feed the
 * heartbeat and the shutdown report. Readers take the torn-read trade every
 * other renderer counter does.
 *
 * WHY IT IS OPT-IN. Every draw and upload reads the clock two or three times.
 * On a native host that is a vDSO read of about 30 ns; in the browser it is a
 * call out of wasm into JavaScript's performance.now(), and on `#test-play`
 * the draw path's clock reads were about 1.5% of the guest worker, more than
 * several of the things they timed. The once-a-frame clock (gpu_frame_timing)
 * is not this one and stays on.
 *
 * Disarmed, the clock reads 0 and nothing accumulates, and the reports say the
 * host share was not timed rather than printing a zero that looks measured.
 */
#ifndef X2_GPU_HOST_TIMER_H
#define X2_GPU_HOST_TIMER_H

typedef struct GpuHostTimes {
  unsigned long long draw_ns;
  unsigned long long upload_ns;
  unsigned long long upload_alloc_ns;  /* reserve + map + copy + unmap */
  unsigned long long upload_record_ns; /* recording the copy into the batch */
} GpuHostTimes;

/* Nonzero when `gpu.host_timing` is set. Read once, at the first call. */
int gpu_host_timer_armed(void);

/* Monotonic nanoseconds when armed; 0 when not. */
unsigned long long gpu_host_timer_ns(void);

/* An accepted draw that began at `t0`. */
void gpu_host_timer_draw(unsigned long long t0);

/* An upload that began at `t0` and had its bytes staged at `staged`. */
void gpu_host_timer_upload(unsigned long long t0, unsigned long long staged);

/* Everything since start. */
GpuHostTimes gpu_host_timer_totals(void);

/* The frame owner's pair: gpu_frame_begin resets, gpu_frame_end reads. */
void gpu_host_timer_frame_reset(void);
GpuHostTimes gpu_host_timer_frame(void);

#endif /* X2_GPU_HOST_TIMER_H */
