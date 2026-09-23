#include "../native/x2_log.h"
/* See gpu_frame_submit.h. */
#include "gpu_frame_submit.h"

#include "gpu_device.h"

#include <SDL3/SDL.h>

#include <stdio.h>

/*
 * The fences of windowless frames still in flight, oldest at g_next once the
 * ring is full. A swapchain blocks acquisition while its images are in flight;
 * the headless target has none, so without a bound an unbounded run enqueues
 * frames and transfer-buffer cycles until the driver runs out of immediately
 * reusable storage. Waiting for the frame submitted kGpuFramesInFlight frames
 * ago supplies that bound without serialising the guest's CPU work against
 * the GPU's, which waiting for the frame just submitted did.
 */
static SDL_GPUFence *g_in_flight[kGpuFramesInFlight];
static unsigned g_next;

static int wait_and_release(SDL_GPUDevice *device, SDL_GPUFence *fence) {
  const int done = SDL_WaitForGPUFences(device, true, &fence, 1);
  if (!done)
    x2_log_error("gpu: waiting for a windowless frame failed: %s\n",
                 SDL_GetError());
  SDL_ReleaseGPUFence(device, fence);
  return done;
}

void gpu_frame_submit_drain(SDL_GPUDevice *device) {
  for (unsigned i = 0; i < kGpuFramesInFlight; i++) {
    SDL_GPUFence *const fence = g_in_flight[(g_next + i) % kGpuFramesInFlight];
    if (fence)
      (void)wait_and_release(device, fence);
  }
  for (unsigned i = 0; i < kGpuFramesInFlight; i++)
    g_in_flight[i] = NULL;
  g_next = 0;
}

int gpu_frame_submit(SDL_GPUDevice *device, SDL_GPUCommandBuffer *command,
                     GpuFrameWait wait) {
  if (wait == kGpuFrameWaitNone) {
    if (SDL_SubmitGPUCommandBuffer(command))
      return 1;
    x2_log_error("gpu: frame submission failed: %s\n", SDL_GetError());
    return 0;
  }
  SDL_GPUFence *const fence =
      SDL_SubmitGPUCommandBufferAndAcquireFence(command);
  if (!fence) {
    x2_log_error("gpu: windowless frame submission did not return a "
                 "fence: %s\n",
                 SDL_GetError());
    return 0;
  }
  if (wait == kGpuFrameWaitComplete) {
    /* One queue completes in order, so every frame held before this one has
       finished once it has. */
    const int done = wait_and_release(device, fence);
    gpu_frame_submit_drain(device);
    return done;
  }
  SDL_GPUFence *const oldest = g_in_flight[g_next];
  g_in_flight[g_next] = fence;
  g_next = (g_next + 1u) % kGpuFramesInFlight;
  return oldest ? wait_and_release(device, oldest) : 1;
}
