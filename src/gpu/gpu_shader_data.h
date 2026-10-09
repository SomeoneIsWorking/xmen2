#pragma once

#ifdef X2_WITH_SDL
#include <SDL3/SDL.h>

#include <cstddef>

namespace x2::gpu {

/* The embedded representation and requested device format share one contract.
 */
#ifdef __EMSCRIPTEN__
using GpuShaderWord = char;
inline constexpr SDL_GPUShaderFormat X2_GPU_SHADER_FORMAT =
    SDL_GPU_SHADERFORMAT_WGSL;
template <typename Word, std::size_t Count>
constexpr std::size_t X2_GPU_SHADER_SIZE(const Word (&)[Count]) {
  return sizeof(Word[Count]) - 1;
}
#else
using GpuShaderWord = unsigned int;
inline constexpr SDL_GPUShaderFormat X2_GPU_SHADER_FORMAT =
    SDL_GPU_SHADERFORMAT_SPIRV;
template <typename Word, std::size_t Count>
constexpr std::size_t X2_GPU_SHADER_SIZE(const Word (&)[Count]) {
  return sizeof(Word[Count]);
}
#endif

} // namespace x2::gpu
#endif
