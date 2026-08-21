/* Submission policy for swapchain-backed and windowless frames. */
#ifndef GPU_FRAME_SUBMIT_H
#define GPU_FRAME_SUBMIT_H

struct SDL_GPUCommandBuffer;
struct SDL_GPUDevice;

/* Windowless targets have no swapchain acquisition to bound queued frames. */
int gpu_frame_submit(struct SDL_GPUDevice *device,
                     struct SDL_GPUCommandBuffer *command,
                     int wait_for_completion);

#endif
