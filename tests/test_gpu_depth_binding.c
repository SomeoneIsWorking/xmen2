#include "gpu_depth_binding.h"

#include <stdio.h>
#include <stdlib.h>

static int failure, textures, samplers, cancels, submissions, probes, checks;
static int device_token, texture_token, sampler_token, command_token,
    pass_token;
#define DEVICE ((SDL_GPUDevice *)&device_token)
#define TEXTURE ((SDL_GPUTexture *)&texture_token)
#define SAMPLER ((SDL_GPUSampler *)&sampler_token)
#define COMMAND ((SDL_GPUCommandBuffer *)&command_token)
#define PASS ((SDL_GPURenderPass *)&pass_token)
#define CHECK(value)                                                           \
  do {                                                                         \
    ++checks;                                                                  \
    if (!(value)) {                                                            \
      fprintf(stderr, "depth binding check failed: %s, line %d\n", #value,     \
              __LINE__);                                                       \
      exit(1);                                                                 \
    }                                                                          \
  } while (0)

bool x2_test_GPUTextureSupportsFormat(SDL_GPUDevice *device,
                                      SDL_GPUTextureFormat format,
                                      SDL_GPUTextureType type,
                                      SDL_GPUTextureUsageFlags usage) {
  CHECK(device == DEVICE && type == SDL_GPU_TEXTURETYPE_2D);
  CHECK(usage == (SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET |
                  SDL_GPU_TEXTUREUSAGE_SAMPLER));
  ++probes;
  return failure != 6 && format == SDL_GPU_TEXTUREFORMAT_D16_UNORM;
}
SDL_GPUTexture *x2_test_CreateGPUTexture(SDL_GPUDevice *device,
                                         const SDL_GPUTextureCreateInfo *info) {
  CHECK(device == DEVICE && info->format == SDL_GPU_TEXTUREFORMAT_D16_UNORM);
  CHECK(info->width == 1 && info->height == 1 && info->num_levels == 1);
  textures += failure != 1;
  return failure == 1 ? NULL : TEXTURE;
}
SDL_GPUSampler *x2_test_CreateGPUSampler(SDL_GPUDevice *device,
                                         const SDL_GPUSamplerCreateInfo *info) {
  CHECK(device == DEVICE && !info->enable_compare);
  samplers += failure != 2;
  return failure == 2 ? NULL : SAMPLER;
}
SDL_GPUCommandBuffer *x2_test_AcquireGPUCommandBuffer(SDL_GPUDevice *device) {
  CHECK(device == DEVICE);
  return failure == 3 ? NULL : COMMAND;
}
SDL_GPURenderPass *
x2_test_BeginGPURenderPass(SDL_GPUCommandBuffer *command,
                           const SDL_GPUColorTargetInfo *colors, Uint32 count,
                           const SDL_GPUDepthStencilTargetInfo *depth) {
  CHECK(command == COMMAND && colors == NULL && count == 0);
  CHECK(depth->texture == TEXTURE && depth->clear_depth == 1.0f);
  CHECK(depth->load_op == SDL_GPU_LOADOP_CLEAR &&
        depth->store_op == SDL_GPU_STOREOP_STORE);
  return failure == 4 ? NULL : PASS;
}
void x2_test_EndGPURenderPass(SDL_GPURenderPass *pass) { CHECK(pass == PASS); }
bool x2_test_SubmitGPUCommandBuffer(SDL_GPUCommandBuffer *command) {
  CHECK(command == COMMAND);
  ++submissions;
  return failure != 5;
}
bool x2_test_CancelGPUCommandBuffer(SDL_GPUCommandBuffer *command) {
  CHECK(command == COMMAND);
  ++cancels;
  return true;
}
void x2_test_ReleaseGPUTexture(SDL_GPUDevice *device, SDL_GPUTexture *texture) {
  CHECK(device == DEVICE && texture == TEXTURE);
  --textures;
}
void x2_test_ReleaseGPUSampler(SDL_GPUDevice *device, SDL_GPUSampler *sampler) {
  CHECK(device == DEVICE && sampler == SAMPLER);
  --samplers;
}

int main(void) {
  for (failure = 1; failure <= 6; ++failure) {
    CHECK(!gpu_depth_binding_create(DEVICE));
    CHECK(textures == 0 && samplers == 0);
    CHECK(gpu_depth_binding_get().texture == NULL);
    CHECK(gpu_depth_binding_get().sampler == NULL);
  }
  CHECK(cancels == 1);
  failure = 0;
  CHECK(gpu_depth_binding_create(DEVICE));
  CHECK(textures == 1 && samplers == 1);
  CHECK(gpu_depth_binding_get().texture == TEXTURE);
  CHECK(gpu_depth_binding_get().sampler == SAMPLER);
  int previous_probes = probes;
  CHECK(gpu_depth_binding_create(DEVICE));
  CHECK(probes == previous_probes && submissions == 2);
  gpu_depth_binding_destroy(DEVICE);
  gpu_depth_binding_destroy(DEVICE);
  CHECK(textures == 0 && samplers == 0);
  CHECK(gpu_depth_binding_get().texture == NULL);
  printf("depth binding: 6 failure boundaries, reuse and teardown; %d checks\n",
         checks);
  return 0;
}
