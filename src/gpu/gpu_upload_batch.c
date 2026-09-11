/* See gpu_upload_batch.h. */
#include "gpu_upload_batch.h"

#include "../native/x2_log.h"

#include <SDL3/SDL.h>

static SDL_GPUCommandBuffer *g_command;
static SDL_GPUCopyPass *g_pass;
static unsigned long g_batches;

SDL_GPUCopyPass *gpu_upload_batch_pass(SDL_GPUDevice *device) {
  if (!device)
    return NULL;
  if (g_pass)
    return g_pass;
  g_command = SDL_AcquireGPUCommandBuffer(device);
  if (!g_command) {
    x2_log_error("gpu: no command buffer for this frame's uploads: %s\n",
                 SDL_GetError());
    return NULL;
  }
  g_pass = SDL_BeginGPUCopyPass(g_command);
  if (!g_pass) {
    x2_log_error("gpu: the upload copy pass could not be opened: %s\n",
                 SDL_GetError());
    SDL_CancelGPUCommandBuffer(g_command);
    g_command = NULL;
    return NULL;
  }
  return g_pass;
}

void gpu_upload_batch_flush(SDL_GPUDevice *device) {
  (void)device;
  if (!g_pass)
    return;
  SDL_EndGPUCopyPass(g_pass);
  g_pass = NULL;
  /*
   * Not fenced. SDL_GPU executes submitted command buffers in order and
   * tracks the resources they touch, so a draw submitted after this batch
   * sees it.
   */
  if (!SDL_SubmitGPUCommandBuffer(g_command))
    x2_log_error("gpu: submitting this frame's uploads failed: %s\n",
                 SDL_GetError());
  g_command = NULL;
  g_batches++;
}

unsigned long gpu_upload_batch_submits(void) { return g_batches; }
