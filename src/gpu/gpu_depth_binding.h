#pragma once

#ifdef X2_WITH_SDL
#include <SDL3/SDL.h>

namespace x2::gpu {

SDL_GPUTextureFormat gpu_sampleable_depth_format(SDL_GPUDevice *device);
/* Owns a depth-typed, compare-sampled neutral binding for shaders whose shadow
 * branch is off. */
bool gpu_depth_binding_create(SDL_GPUDevice *device);
SDL_GPUTextureSamplerBinding gpu_depth_binding_get(void);
void gpu_depth_binding_destroy(SDL_GPUDevice *device);

} // namespace x2::gpu
#endif
