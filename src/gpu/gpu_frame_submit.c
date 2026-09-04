#include "../native/x2_log.h"
/* See gpu_frame_submit.h. */
#include "gpu_frame_submit.h"

#include <SDL3/SDL.h>

#include <stdio.h>

int gpu_frame_submit(SDL_GPUDevice *device, SDL_GPUCommandBuffer *command,
                     int wait_for_completion) {
  SDL_GPUFence *fence;

  if (!wait_for_completion) {
    if (SDL_SubmitGPUCommandBuffer(command))
      return 1;
    x2_log_error("gpu: frame submission failed: %s\n", SDL_GetError());
    return 0;
  }

  /*
   * A swapchain blocks acquisition when its images are in flight. The
   * headless target has no acquisition step, so without this fence an
   * unbounded test can enqueue frames and transfer-buffer cycles until the
   * driver runs out of immediately reusable storage. Completing one frame
   * at this boundary supplies the missing backpressure without changing the
   * commands or pixels inside the frame.
   */
  fence = SDL_SubmitGPUCommandBufferAndAcquireFence(command);
  if (!fence) {
    x2_log_error("gpu: windowless frame submission did not return a "
                 "fence: %s\n",
                 SDL_GetError());
    return 0;
  }
  if (!SDL_WaitForGPUFences(device, true, &fence, 1)) {
    x2_log_error("gpu: waiting for a windowless frame failed: %s\n",
                 SDL_GetError());
    SDL_ReleaseGPUFence(device, fence);
    return 0;
  }
  SDL_ReleaseGPUFence(device, fence);
  return 1;
}
