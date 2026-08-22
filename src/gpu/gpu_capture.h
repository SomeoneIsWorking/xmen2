#ifndef X2_GPU_CAPTURE_H
#define X2_GPU_CAPTURE_H

#include <stdint.h>

/* Apply the X2_SHOT policy at the safe, same-thread end-of-frame boundary. */
void gpu_capture_frame(int headless, unsigned long frame,
                       uint32_t width, uint32_t height);

#endif
