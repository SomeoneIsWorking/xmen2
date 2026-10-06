#ifndef X2_GPU_DEPTH_BINDING_H
#define X2_GPU_DEPTH_BINDING_H

#ifdef X2_WITH_SDL
#include <SDL3/SDL.h>

SDL_GPUTextureFormat gpu_sampleable_depth_format(SDL_GPUDevice *device);
/* Owns a depth-typed neutral binding for shaders whose shadow branch is off. */
bool gpu_depth_binding_create(SDL_GPUDevice *device);
SDL_GPUTextureSamplerBinding gpu_depth_binding_get(void);
void gpu_depth_binding_destroy(SDL_GPUDevice *device);
#endif

#endif
