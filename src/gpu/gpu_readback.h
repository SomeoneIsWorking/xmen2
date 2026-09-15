/*
 * Reading a GPU texture back to CPU bytes.
 *
 * Two owners need exactly this: the off-screen read used by the selftests and
 * `X2_SHOT` (gpu_draw.c), and the presented-frame luma probe, which samples
 * the logical D3D backbuffer to separate "the engine drew black" from "the
 * composite presented black". The fence/pass-completion choreography differs
 * per caller, so the shared helper is only: download one texture region
 * through a bounded transfer buffer, wait its own fence, copy it out. The
 * helper reports its own failure with the SDL message; silence here is the
 * exact lie these probes exist to avoid.
 */
#ifndef X2_GPU_READBACK_H
#define X2_GPU_READBACK_H

#include <SDL3/SDL_gpu.h>
#include <stdint.h>

/* Download a width x height texture (one subresource, 4 bytes per pixel) into
   `out`, where out_bytes >= width*height*4, submitting and waiting its own
   fence. The texture must not sit in an open render or copy pass. Returns 1
   on success; on any failure the reason is logged and 0 returned. */
int gpu_readback_texture_rgba(SDL_GPUDevice *device, SDL_GPUTexture *texture,
                              uint32_t width, uint32_t height, void *out,
                              uint32_t out_bytes);

#endif /* X2_GPU_READBACK_H */
