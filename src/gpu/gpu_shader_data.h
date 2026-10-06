#ifndef X2_GPU_SHADER_DATA_H
#define X2_GPU_SHADER_DATA_H

#ifdef X2_WITH_SDL
#include <SDL3/SDL.h>

/* The embedded representation and requested device format share one contract.
 */
#ifdef __EMSCRIPTEN__
typedef char GpuShaderWord;
#define X2_GPU_SHADER_FORMAT SDL_GPU_SHADERFORMAT_WGSL
#define X2_GPU_SHADER_SIZE(data) (sizeof(data) - 1)
#else
typedef unsigned int GpuShaderWord;
#define X2_GPU_SHADER_FORMAT SDL_GPU_SHADERFORMAT_SPIRV
#define X2_GPU_SHADER_SIZE(data) sizeof(data)
#endif
#endif

#endif
