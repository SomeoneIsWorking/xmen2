#include "gpu_depth_binding.h"

#ifdef X2_WITH_SDL
static SDL_GPUTextureSamplerBinding neutral;

SDL_GPUTextureFormat gpu_sampleable_depth_format(SDL_GPUDevice *device) {
  static const SDL_GPUTextureFormat formats[] = {
      SDL_GPU_TEXTUREFORMAT_D32_FLOAT,
      SDL_GPU_TEXTUREFORMAT_D16_UNORM,
      SDL_GPU_TEXTUREFORMAT_D24_UNORM,
  };
  const SDL_GPUTextureUsageFlags usage =
      SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
  for (unsigned i = 0; i < SDL_arraysize(formats); ++i) {
    if (SDL_GPUTextureSupportsFormat(device, formats[i], SDL_GPU_TEXTURETYPE_2D,
                                     usage)) {
      return formats[i];
    }
  }
  return SDL_GPU_TEXTUREFORMAT_INVALID;
}

bool gpu_depth_binding_create(SDL_GPUDevice *device) {
  SDL_GPUTextureCreateInfo info = {0};
  SDL_GPUSamplerCreateInfo sampler = {0};
  SDL_GPUDepthStencilTargetInfo target = {0};
  SDL_GPUCommandBuffer *command;
  SDL_GPURenderPass *pass;
  if (neutral.texture && neutral.sampler) {
    return true;
  }
  info.type = SDL_GPU_TEXTURETYPE_2D;
  info.format = gpu_sampleable_depth_format(device);
  info.usage =
      SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
  info.width = info.height = info.layer_count_or_depth = info.num_levels = 1;
  if (info.format == SDL_GPU_TEXTUREFORMAT_INVALID) {
    return SDL_SetError("No sampleable depth format for shader binding");
  }
  neutral.texture = SDL_CreateGPUTexture(device, &info);
  sampler.min_filter = sampler.mag_filter = SDL_GPU_FILTER_NEAREST;
  sampler.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
  sampler.address_mode_u = sampler.address_mode_v = sampler.address_mode_w =
      SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  neutral.sampler = SDL_CreateGPUSampler(device, &sampler);
  if (!neutral.texture || !neutral.sampler) {
    gpu_depth_binding_destroy(device);
    return false;
  }
  command = SDL_AcquireGPUCommandBuffer(device);
  if (!command) {
    gpu_depth_binding_destroy(device);
    return false;
  }
  target.texture = neutral.texture;
  target.clear_depth = 1.0f;
  target.load_op = SDL_GPU_LOADOP_CLEAR;
  target.store_op = SDL_GPU_STOREOP_STORE;
  target.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
  target.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
  pass = SDL_BeginGPURenderPass(command, NULL, 0, &target);
  if (!pass) {
    SDL_CancelGPUCommandBuffer(command);
    gpu_depth_binding_destroy(device);
    return false;
  }
  SDL_EndGPURenderPass(pass);
  if (!SDL_SubmitGPUCommandBuffer(command)) {
    gpu_depth_binding_destroy(device);
    return false;
  }
  return true;
}

SDL_GPUTextureSamplerBinding gpu_depth_binding_get(void) { return neutral; }

void gpu_depth_binding_destroy(SDL_GPUDevice *device) {
  if (neutral.texture) {
    SDL_ReleaseGPUTexture(device, neutral.texture);
  }
  if (neutral.sampler) {
    SDL_ReleaseGPUSampler(device, neutral.sampler);
  }
  neutral = (SDL_GPUTextureSamplerBinding){0};
}
#endif
