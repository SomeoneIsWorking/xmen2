/* Logical D3D colour/depth targets -> physical SDL presentation. */
#include "gpu_present.h"

#include "aspect_fit.h"
#include "boot_blackout.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

static SDL_GPUTexture *g_scene;
static uint32_t g_scene_width, g_scene_height;
static SDL_GPUTexture *g_depth;
static SDL_GPUTextureFormat g_depth_format = SDL_GPU_TEXTUREFORMAT_INVALID;
static uint32_t g_depth_width, g_depth_height;

static SDL_GPUTexture *make_target(SDL_GPUDevice *device, uint32_t width,
                                   uint32_t height, SDL_GPUTextureFormat format,
                                   SDL_GPUTextureUsageFlags usage) {
  SDL_GPUTextureCreateInfo info;

  memset(&info, 0, sizeof info);
  info.type = SDL_GPU_TEXTURETYPE_2D;
  info.format = format;
  info.usage = usage;
  info.width = width;
  info.height = height;
  info.layer_count_or_depth = 1;
  info.num_levels = 1;
  return SDL_CreateGPUTexture(device, &info);
}

int gpu_present_resize_targets(SDL_GPUDevice *device, uint32_t width,
                               uint32_t height, uint32_t depth_format_value) {
  SDL_GPUTexture *scene_replacement = NULL;
  SDL_GPUTexture *depth_replacement = NULL;
  SDL_GPUTextureFormat depth_format = (SDL_GPUTextureFormat)depth_format_value;
  int replace_scene, replace_depth;

  if (!device) {
    fprintf(stderr, "gpu present: refusing to resize without a GPU "
                    "device.\n");
    return 0;
  }
  if (!width || !height) {
    fprintf(stderr,
            "gpu present: refusing a zero-sized logical D3D "
            "backbuffer (%ux%u).\n",
            width, height);
    return 0;
  }
  replace_scene =
      !g_scene || g_scene_width != width || g_scene_height != height;
  replace_depth = depth_format != SDL_GPU_TEXTUREFORMAT_INVALID &&
                  (!g_depth || g_depth_width != width ||
                   g_depth_height != height || g_depth_format != depth_format);
  if (!replace_scene && !replace_depth)
    return 1;

  if (replace_scene) {
    scene_replacement = make_target(
        device, width, height, SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM,
        SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER);
  }
  if (replace_scene && !scene_replacement) {
    fprintf(stderr,
            "gpu present: could not create a replacement logical "
            "%ux%u D3D backbuffer: %s. Keeping the existing "
            "%ux%u scene.\n",
            width, height, SDL_GetError(), g_scene_width, g_scene_height);
    return 0;
  }
  if (replace_depth)
    depth_replacement = make_target(device, width, height, depth_format,
                                    SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET);
  if (replace_depth && !depth_replacement) {
    if (scene_replacement)
      SDL_ReleaseGPUTexture(device, scene_replacement);
    fprintf(stderr,
            "gpu present: could not create a replacement logical "
            "%ux%u depth target: %s. Keeping the existing colour "
            "and depth targets.\n",
            width, height, SDL_GetError());
    return 0;
  }

  if (scene_replacement) {
    if (g_scene)
      SDL_ReleaseGPUTexture(device, g_scene);
    g_scene = scene_replacement;
    g_scene_width = width;
    g_scene_height = height;
  }
  if (depth_replacement) {
    if (g_depth)
      SDL_ReleaseGPUTexture(device, g_depth);
    g_depth = depth_replacement;
    g_depth_width = width;
    g_depth_height = height;
    g_depth_format = depth_format;
  }
  printf("gpu present: logical D3D backbuffer is %ux%u; the window is "
         "a separate aspect-fitted output.\n",
         width, height);
  return 1;
}

int gpu_present_is_configured(void) {
  return g_scene_width != 0 && g_scene_height != 0;
}

SDL_GPUTexture *gpu_present_scene(SDL_GPUDevice *device, uint32_t *width,
                                  uint32_t *height) {
  if (!device || !g_scene || !gpu_present_is_configured())
    return NULL;
  if (width)
    *width = g_scene_width;
  if (height)
    *height = g_scene_height;
  return g_scene;
}

SDL_GPUTexture *gpu_present_depth_target(SDL_GPUDevice *device, uint32_t width,
                                         uint32_t height,
                                         uint32_t depth_format_value) {
  SDL_GPUTexture *replacement;
  SDL_GPUTextureFormat depth_format = (SDL_GPUTextureFormat)depth_format_value;

  if (!device || !width || !height ||
      depth_format == SDL_GPU_TEXTUREFORMAT_INVALID)
    return NULL;
  if (g_depth && g_depth_width == width && g_depth_height == height &&
      g_depth_format == depth_format)
    return g_depth;
  replacement = make_target(device, width, height, depth_format,
                            SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET);
  if (!replacement) {
    fprintf(stderr,
            "gpu: the %ux%u depth target could not be made: %s -- "
            "keeping the existing depth target.\n",
            width, height, SDL_GetError());
    return NULL;
  }
  if (g_depth)
    SDL_ReleaseGPUTexture(device, g_depth);
  g_depth = replacement;
  g_depth_width = width;
  g_depth_height = height;
  g_depth_format = depth_format;
  return g_depth;
}

int gpu_present_composite(SDL_GPUCommandBuffer *command_buffer,
                          SDL_GPUTexture *output, uint32_t output_width,
                          uint32_t output_height) {
  SDL_GPUBlitInfo info;
  X2AspectRect destination;

  if (!command_buffer || !output || !g_scene ||
      !x2_aspect_fit(output_width, output_height, g_scene_width, g_scene_height,
                     &destination)) {
    fprintf(stderr,
            "gpu present: refusing to composite logical %ux%u "
            "into output %ux%u; a texture or valid size is "
            "missing.\n",
            g_scene_width, g_scene_height, output_width, output_height);
    return 0;
  }

  memset(&info, 0, sizeof info);
  info.source.texture = g_scene;
  info.source.w = g_scene_width;
  info.source.h = g_scene_height;
  info.destination.texture = output;
  info.destination.x = destination.x;
  info.destination.y = destination.y;
  info.destination.w = destination.width;
  info.destination.h = destination.height;
  info.load_op = SDL_GPU_LOADOP_CLEAR;
  info.clear_color.a = 1.0f;
  info.filter = SDL_GPU_FILTER_LINEAR;
  SDL_BlitGPUTexture(command_buffer, &info);
  return 1;
}

void gpu_present_shutdown(SDL_GPUDevice *device) {
  if (device && g_scene)
    SDL_ReleaseGPUTexture(device, g_scene);
  if (device && g_depth)
    SDL_ReleaseGPUTexture(device, g_depth);
  g_scene = NULL;
  g_scene_width = 0;
  g_scene_height = 0;
  g_depth = NULL;
  g_depth_width = 0;
  g_depth_height = 0;
  g_depth_format = SDL_GPU_TEXTUREFORMAT_INVALID;
}

void gpu_present_boot_blackout(SDL_GPUCommandBuffer *command_buffer,
                               struct SDL_GPUTexture *target) {
  SDL_GPUColorTargetInfo ct;

  if (!command_buffer || !target)
    return;
  SDL_zero(ct);
  ct.texture = target;
  ct.clear_color = (SDL_FColor){0.0f, 0.0f, 0.0f, 1.0f};
  ct.load_op = SDL_GPU_LOADOP_CLEAR;
  ct.store_op = SDL_GPU_STOREOP_STORE;
  {
    SDL_GPURenderPass *pass =
        SDL_BeginGPURenderPass(command_buffer, &ct, 1, NULL);
    if (pass)
      SDL_EndGPURenderPass(pass);
  }
  x2_boot_blackout_frame_presented();
}
