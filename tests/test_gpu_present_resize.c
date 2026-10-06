#include "gpu_present.h"

#include <SDL3/SDL.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static int checks;
static unsigned creates, colour_creates, depth_creates, releases;
static SDL_GPUTexture *next_colour;
static SDL_GPUTexture *next_depth;
static SDL_GPUTexture *released_texture[8];

static void check(int condition, const char *expression, int line) {
  checks++;
  if (condition)
    return;
  fprintf(stderr, "test_gpu_present_resize: check %d failed at line %d: %s\n",
          checks, line, expression);
  exit(1);
}

#define CHECK(c) check((c), #c, __LINE__)

/* CMake replaces SDL_CreateGPUTexture/SDL_ReleaseGPUTexture with these names.
   The test therefore exercises the shipping transaction while controlling
   colour and depth allocation independently. */
SDL_GPUTexture *
x2_test_create_gpu_texture(SDL_GPUDevice *device,
                           const SDL_GPUTextureCreateInfo *info) {
  (void)device;
  CHECK(info->width != 0 && info->height != 0);
  creates++;
  if (info->usage & SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET) {
    depth_creates++;
    return next_depth;
  }
  CHECK(info->usage & SDL_GPU_TEXTUREUSAGE_COLOR_TARGET);
  colour_creates++;
  return next_colour;
}

void x2_test_release_gpu_texture(SDL_GPUDevice *device,
                                 SDL_GPUTexture *texture) {
  (void)device;
  released_texture[releases++] = texture;
}

void x2_boot_blackout_frame_presented(void) {}

int main(void) {
  SDL_GPUDevice *device = (SDL_GPUDevice *)(uintptr_t)0x1000;
  SDL_GPUTexture *first_colour = (SDL_GPUTexture *)(uintptr_t)0x2000;
  SDL_GPUTexture *first_depth = (SDL_GPUTexture *)(uintptr_t)0x2100;
  SDL_GPUTexture *second_colour = (SDL_GPUTexture *)(uintptr_t)0x3000;
  SDL_GPUTexture *second_depth = (SDL_GPUTexture *)(uintptr_t)0x3100;
  const uint32_t depth_format = SDL_GPU_TEXTUREFORMAT_D16_UNORM;
  uint32_t width = 0, height = 0;

  next_colour = first_colour;
  next_depth = first_depth;
  CHECK(gpu_present_resize_targets(device, 1280, 720, depth_format));
  CHECK(creates == 2 && colour_creates == 1 && depth_creates == 1);
  CHECK(releases == 0);
  CHECK(gpu_present_scene(device, &width, &height) == first_colour);
  CHECK(width == 1280 && height == 720);
  CHECK(gpu_present_depth_target(device, 1280, 720, depth_format) ==
        first_depth);

  /* Colour failure does not attempt depth allocation or disturb either
     active target. */
  next_colour = NULL;
  next_depth = second_depth;
  CHECK(!gpu_present_resize_targets(device, 1600, 900, depth_format));
  CHECK(creates == 3 && colour_creates == 2 && depth_creates == 1);
  CHECK(releases == 0);
  CHECK(gpu_present_scene(device, &width, &height) == first_colour);
  CHECK(gpu_present_depth_target(device, 1280, 720, depth_format) ==
        first_depth);

  /* Depth failure releases only the prepared colour replacement. The old
     pair and its proven dimensions remain active together. */
  next_colour = second_colour;
  next_depth = NULL;
  CHECK(!gpu_present_resize_targets(device, 1920, 1080, depth_format));
  CHECK(creates == 5 && colour_creates == 3 && depth_creates == 2);
  CHECK(releases == 1 && released_texture[0] == second_colour);
  CHECK(gpu_present_scene(device, &width, &height) == first_colour);
  CHECK(width == 1280 && height == 720);
  CHECK(gpu_present_depth_target(device, 1280, 720, depth_format) ==
        first_depth);

  next_colour = second_colour;
  next_depth = second_depth;
  CHECK(gpu_present_resize_targets(device, 1920, 1080, depth_format));
  CHECK(creates == 7 && releases == 3);
  CHECK(released_texture[1] == first_colour &&
        released_texture[2] == first_depth);
  CHECK(gpu_present_scene(device, &width, &height) == second_colour);
  CHECK(width == 1920 && height == 1080);
  CHECK(gpu_present_depth_target(device, 1920, 1080, depth_format) ==
        second_depth);

  /* Selecting the active size is a no-op, not an allocation churn. */
  CHECK(gpu_present_resize_targets(device, 1920, 1080, depth_format));
  CHECK(creates == 7 && releases == 3);
  CHECK(!gpu_present_resize_targets(device, 0, 1080, depth_format));
  CHECK(gpu_present_scene(device, &width, &height) == second_colour);

  gpu_present_shutdown(device);
  CHECK(releases == 5);
  CHECK(released_texture[3] == second_colour &&
        released_texture[4] == second_depth);
  CHECK(!gpu_present_is_configured());

  printf("test_gpu_present_resize: %d checks passed\n", checks);
  return 0;
}
