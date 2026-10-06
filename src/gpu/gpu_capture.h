#ifndef X2_GPU_CAPTURE_H
#define X2_GPU_CAPTURE_H

#include <stddef.h>
#include <stdint.h>

/*
 * One control-channel capture at a time.
 *
 * A request is armed on the render thread, completed at that thread's next
 * end-of-frame boundary, and retained as BGRA8 until the caller consumes it.
 * `gpu_capture_result` returns 1 when ready, 0 while a frame is still pending,
 * and -1 with an explicit reason after refusal/failure.
 */
int gpu_capture_request(char *why, int whyn);
int gpu_capture_result(const unsigned char **bgra, uint32_t *width,
                       uint32_t *height, char *why, int whyn);
void gpu_capture_discard(void);

/* Observe final-frame submission/readback completion on the render thread. */
void gpu_capture_set_frame_observer(void (*fn)(void));
void gpu_capture_shutdown(void);

/* Apply the X2_SHOT policy at the safe, same-thread end-of-frame boundary. */
void gpu_capture_frame(int headless, unsigned long frame, uint32_t width,
                       uint32_t height);

#endif
