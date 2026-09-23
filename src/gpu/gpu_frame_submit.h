/* Submission policy for swapchain-backed and windowless frames. */
#ifndef GPU_FRAME_SUBMIT_H
#define GPU_FRAME_SUBMIT_H

struct SDL_GPUCommandBuffer;
struct SDL_GPUDevice;

typedef enum GpuFrameWait {
  /* A swapchain bounds the frames in flight by blocking acquisition. */
  kGpuFrameWaitNone,
  /* A windowless target has no acquisition, so submission applies the same
     bound itself: at most kGpuFramesInFlight frames submitted and unfinished.
   */
  kGpuFrameWaitBounded,
  /* The caller reads this frame back, so it must have finished. */
  kGpuFrameWaitComplete,
} GpuFrameWait;

int gpu_frame_submit(struct SDL_GPUDevice *device,
                     struct SDL_GPUCommandBuffer *command, GpuFrameWait wait);

/* Wait for every frame still held in flight and release its fence; before
   the device is destroyed. */
void gpu_frame_submit_drain(struct SDL_GPUDevice *device);

#endif
