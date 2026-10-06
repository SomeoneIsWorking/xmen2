#include "d3d8_live_resolution.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks;
static int frame_open;
static int gpu_accepts = 1;
static unsigned gpu_calls;
static uint32_t gpu_width, gpu_height;
static unsigned viewport_calls;
static int viewport_width, viewport_height;

static void check(int condition, const char *expression, int line) {
  checks++;
  if (condition)
    return;
  fprintf(stderr, "test_d3d8_live_resolution: check %d failed at line %d: %s\n",
          checks, line, expression);
  exit(1);
}

#define CHECK(c) check((c), #c, __LINE__)

int gpu_frame_in_progress(void) { return frame_open; }

int gpu_device_set_backbuffer_size(uint32_t width, uint32_t height) {
  gpu_calls++;
  gpu_width = width;
  gpu_height = height;
  return gpu_accepts;
}

void gpu_frame_viewport(int x, int y, int width, int height, float minz,
                        float maxz) {
  CHECK(x == 0 && y == 0 && minz == 0.0f && maxz == 1.0f);
  viewport_calls++;
  viewport_width = width;
  viewport_height = height;
}

static D3D8Surface surface(D3D8SurfaceKind kind, uint32_t width,
                           uint32_t height, uint32_t bpp) {
  D3D8Surface out;
  memset(&out, 0, sizeof out);
  out.kind = kind;
  out.width = width;
  out.height = height;
  out.bytes_per_pixel = bpp;
  out.pitch = width * bpp;
  out.size = out.pitch * height;
  return out;
}

int main(void) {
  D3DPRESENT_PARAMETERS parameters;
  D3D8Surface backbuffer = surface(D3D8_SURF_BACKBUFFER, 1280, 720, 4);
  D3D8Surface depth = surface(D3D8_SURF_DEPTHSTENCIL, 1280, 720, 2);
  D3D8State state;
  char why[256];

  memset(&parameters, 0, sizeof parameters);
  memset(&state, 0, sizeof state);
  parameters.BackBufferWidth = 1280;
  parameters.BackBufferHeight = 720;
  parameters.BackBufferFormat = 22;
  state.viewport_x = 13;
  state.viewport_y = 9;
  state.viewport_w = 640;
  state.viewport_h = 360;
  state.viewport_minz = 0.25f;
  state.viewport_maxz = 0.75f;
  state.viewport_set = 1;

  d3d8_live_resolution_bind(&parameters, &backbuffer, &depth, &state);
  CHECK(d3d8_live_resolution_apply(1920, 1080, why, sizeof why));
  CHECK(gpu_calls == 1 && gpu_width == 1920 && gpu_height == 1080);
  CHECK(parameters.BackBufferWidth == 1920 &&
        parameters.BackBufferHeight == 1080);
  CHECK(parameters.BackBufferFormat == 22);
  CHECK(backbuffer.width == 1920 && backbuffer.height == 1080);
  CHECK(backbuffer.pitch == 1920 * 4 && backbuffer.size == 1920 * 1080 * 4);
  CHECK(depth.width == 1920 && depth.height == 1080);
  CHECK(depth.pitch == 1920 * 2 && depth.size == 1920 * 1080 * 2);
  CHECK(state.viewport_x == 0 && state.viewport_y == 0 &&
        state.viewport_w == 1920 && state.viewport_h == 1080);
  CHECK(state.viewport_minz == 0.0f && state.viewport_maxz == 1.0f &&
        state.viewport_set == 1);
  CHECK(viewport_calls == 1 && viewport_width == 1920 &&
        viewport_height == 1080);

  frame_open = 1;
  CHECK(!d3d8_live_resolution_apply(1600, 900, why, sizeof why));
  CHECK(parameters.BackBufferWidth == 1920 && gpu_calls == 1);
  frame_open = 0;

  gpu_accepts = 0;
  CHECK(!d3d8_live_resolution_apply(1600, 900, why, sizeof why));
  CHECK(parameters.BackBufferWidth == 1920 && gpu_calls == 2);
  gpu_accepts = 1;

  CHECK(!d3d8_live_resolution_apply(0, 900, why, sizeof why));
  CHECK(!d3d8_live_resolution_apply(20000, 900, why, sizeof why));
  CHECK(parameters.BackBufferWidth == 1920 && gpu_calls == 2);

  CHECK(d3d8_live_resolution_apply(1280, 720, why, sizeof why));
  CHECK(parameters.BackBufferWidth == 1280 && backbuffer.width == 1280);
  CHECK(depth.width == 1280 && state.viewport_w == 1280);

  d3d8_live_resolution_unbind();
  CHECK(!d3d8_live_resolution_apply(1920, 1080, why, sizeof why));

  printf("test_d3d8_live_resolution: %d checks passed\n", checks);
  return 0;
}
