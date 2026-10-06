/* gpu_frame_epoch.c -- the frame command buffer's acquisition and epoch; see
   gpu_internal.h. */
#ifdef X2_WITH_SDL
#include "gpu_internal.h"

/* Frame command buffers acquired. */
static uint64_t g_frame_epoch;

int gpu_frame_command_acquire(void) {
  g_cmd = SDL_AcquireGPUCommandBuffer(g_gpu);
  if (!g_cmd)
    return 0;
  g_frame_epoch++;
  return 1;
}

uint64_t gpu_frame_epoch(void) { return g_frame_epoch; }

uint64_t gpu_frame_epoch_closed(void) {
  return g_cmd ? g_frame_epoch - 1u : g_frame_epoch;
}
#endif
