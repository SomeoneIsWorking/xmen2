#ifndef X2_GPU_FRAME_TIMING_REPORT_H
#define X2_GPU_FRAME_TIMING_REPORT_H

/* Install the host-share-aware slow-frame diagnostic at the timing boundary. */
void gpu_frame_timing_report_install(void);

/* One heartbeat interval's frame-phase line: present-to-present wall time
   against the host's draw, upload and swapchain-wait shares of it. */
void gpu_frame_timing_report_interval(void);

#endif
