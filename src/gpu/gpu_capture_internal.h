/* End-of-frame mechanics private to the GPU subsystem. */
#ifndef X2_GPU_CAPTURE_INTERNAL_H
#define X2_GPU_CAPTURE_INTERNAL_H

#include <stdint.h>

#ifdef X2_WITH_SDL
#include <SDL3/SDL.h>

/* Redirect final presentation into a retained texture while capture is armed.
   `fallback` remains the target when no request exists or allocation fails. */
SDL_GPUTexture *gpu_capture_frame_target(SDL_GPUDevice *device,
                                         SDL_GPUTexture *fallback,
                                         uint32_t width, uint32_t height);

/* Present a retained window target to `output` when needed, then append its
   readback to the SAME command buffer. Returns 1 only when a readback was
   recorded and the submit therefore has to be fenced. */
int gpu_capture_frame_record(SDL_GPUDevice *device,
                             SDL_GPUCommandBuffer *command,
                             SDL_GPUTexture *rendered, SDL_GPUTexture *output,
                             uint32_t width, uint32_t height);

/* Publish or fail the recorded readback after the command buffer completes. */
void gpu_capture_frame_complete(SDL_GPUDevice *device, int submitted);

/* Apply capture's fence requirement around the ordinary frame submit. */
int gpu_capture_submit_frame(SDL_GPUDevice *device,
                             SDL_GPUCommandBuffer *command,
                             int wait_without_capture, SDL_GPUTexture *rendered,
                             SDL_GPUTexture *output, uint32_t width,
                             uint32_t height);
#endif

#endif
