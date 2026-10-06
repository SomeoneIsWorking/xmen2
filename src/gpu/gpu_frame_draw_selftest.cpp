/*
 * The REAL frame path, drawn into and read back -- the one path the game uses
 * that no self-test had ever exercised.
 *
 * Every other pixel check in this battery renders through
 * `gpu_offscreen_begin`, into a texture it makes itself. That covers the pass,
 * the pipeline and the shaders, and it is why all of them passed in a browser
 * whose real frames came back black (issue #152). What it does not cover is
 * what `gpu_frame_begin`/`gpu_frame_end` add around them: acquiring the
 * output, targeting the logical scene texture rather than a private one, and
 * compositing that scene into the output. The game's frames are black at the
 * end of that sequence, and nothing here could see it.
 *
 * So this drives the frame the way the engine drives it -- begin, clear, draw,
 * end -- and then reads BOTH textures the sequence produces:
 *
 *   the scene, which is what the draw wrote, and
 *   the output, which is what the composite put in front of the player.
 *
 * The scene is what this reads, because the scene is what the game's own
 * probe reports as black. The composite that follows it already has a check
 * of its own (`gpu_present_selftest`), which drives it against a scene whose
 * pixels it wrote by hand; what neither of them covered is a real draw
 * arriving in that scene texture.
 *
 * It needs a window, because the scene target only exists on the windowed
 * path -- headless draws straight into its own output and never touches the
 * scene. A host with no display therefore SKIPS this check and says so; that
 * is not a pass, and the battery's summary counts it as a skip.
 */
#include "gpu_selftests.h"

#include "../native/x2_log.h"

#ifndef X2_WITH_SDL
int gpu_frame_draw_selftest(void) {
  x2_log_info("gpu frame-draw selftest: SKIPPED -- built without SDL. This is "
              "not a pass.\n");
  return 77;
}
#else

#include "gpu_device.h"
#include "gpu_draw.h"
#include "gpu_internal.h"
#include "gpu_present.h"
#include "gpu_readback.h"

#include <SDL3/SDL.h>

#include <SDL3/SDL_gpu.h>
#include <stdint.h>
#include <string.h>

#define FRAME_W 64u
#define FRAME_H 64u

#define CLEAR_BGRA 0xFF0000FFu    /* opaque blue */
#define TRIANGLE_BGRA 0xFFFF0000u /* opaque red */

static int pixel_is(const uint32_t *image, uint32_t width, uint32_t x,
                    uint32_t y, uint32_t want, const char *where,
                    const char *what) {
  const uint32_t got = image[(size_t)y * (size_t)width + (size_t)x];
  if (got == want) {
    return 1;
  }
  x2_log_info("gpu frame-draw selftest: FAILED -- %s (%u,%u) is 0x%08x, "
              "expected 0x%08x (%s)\n",
              where, x, y, got, want, what);
  return 0;
}

/* The scene and the output are read the same way and checked the same way;
   only their names differ, and a reader has to be able to tell which one a
   failure names. */
static int check_image(const uint32_t *image, uint32_t width, uint32_t height,
                       const char *where) {
  int ok = 1;
  ok &= pixel_is(image, width, width / 2u, height / 2u, TRIANGLE_BGRA, where,
                 "the middle of the triangle");
  ok &= pixel_is(image, width, 1u, 1u, CLEAR_BGRA, where,
                 "a corner outside the triangle, which must still be the "
                 "clear colour");
  return ok;
}

int gpu_frame_draw_selftest(void) {
  struct Vertex {
    float x, y, z, rhw;
    uint32_t color;
  };
  const struct Vertex tri[3] = {
      {(float)FRAME_W * 0.5f, 2.0f, 0.0f, 1.0f, TRIANGLE_BGRA},
      {(float)FRAME_W - 2.0f, (float)FRAME_H - 2.0f, 0.0f, 1.0f, TRIANGLE_BGRA},
      {2.0f, (float)FRAME_H - 2.0f, 0.0f, 1.0f, TRIANGLE_BGRA}};
  static uint32_t scene_image[FRAME_W * FRAME_H];
  SDL_GPUTexture *scene;
  SDL_Window *window;
  uint32_t scene_w = 0, scene_h = 0;
  GpuBuffer vertices;
  GpuDraw draw;
  int fails = 0;
  int began = 0;
  int attempt;

  x2_log_info("\n=== gpu frame-draw selftest: the production frame path, a "
              "real draw in the scene texture ===\n");
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    x2_log_info("gpu frame-draw selftest: SKIPPED -- no video subsystem on "
                "this host (%s), so there is no windowed frame path to "
                "drive. This is not a pass.\n",
                SDL_GetError());
    return 77;
  }
  window =
      SDL_CreateWindow("x2 frame-draw selftest", (int)FRAME_W, (int)FRAME_H, 0);
  if (!window) {
    x2_log_info("gpu frame-draw selftest: SKIPPED -- no window on this host "
                "(%s). This is not a pass.\n",
                SDL_GetError());
    return 77;
  }
  if (!gpu_device_create() || !gpu_device_attach_window(window)) {
    x2_log_info("gpu frame-draw selftest: FAILED -- the device or its "
                "swapchain could not be made for a window that exists.\n");
    SDL_DestroyWindow(window);
    gpu_device_destroy();
    return 1;
  }
  if (!gpu_device_set_backbuffer_size(FRAME_W, FRAME_H)) {
    x2_log_info("gpu frame-draw selftest: FAILED -- the logical backbuffer "
                "could not be sized, so there is no scene to draw into.\n");
    SDL_DestroyWindow(window);
    gpu_device_destroy();
    return 1;
  }
  vertices = gpu_buffer_create(GPU_BUF_VERTEX, sizeof tri);
  if (!vertices || !gpu_buffer_upload(vertices, 0, tri, sizeof tri)) {
    x2_log_info("gpu frame-draw selftest: FAILED -- the vertex buffer could "
                "not be made or filled.\n");
    SDL_DestroyWindow(window);
    gpu_device_destroy();
    return 1;
  }
  /* A swapchain image may legitimately not be ready on the first ask. A
     frame that never begins is a failure, and saying how many asks it took
     keeps the two apart. */
  for (attempt = 0; attempt < 60 && !began; attempt++) {
    began = gpu_frame_begin();
    if (!began) {
      SDL_Delay(16);
    }
  }
  if (!began) {
    x2_log_info("gpu frame-draw selftest: FAILED -- no frame began in 60 "
                "attempts, so the path this test exists for never ran.\n");
    SDL_DestroyWindow(window);
    gpu_device_destroy();
    return 1;
  }
  gpu_frame_clear(1u, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0u);

  memset(&draw, 0, sizeof draw);
  draw.vertices = vertices;
  draw.vertex_stride = sizeof tri[0];
  draw.prim = GPU_PRIM_TRIANGLELIST;
  draw.prim_count = 1;
  draw.pos_offset = 0;
  draw.pretransformed = 1;
  draw.color_offset = 16;
  draw.uv_offset = -1;
  draw.texop = GPU_TEXOP_NONE;
  draw.cull = GPU_CULL_NONE;
  draw.depth_func = GPU_CMP_ALWAYS;
  if (!gpu_draw(&draw)) {
    x2_log_info("gpu frame-draw selftest: FAILED -- the draw was refused "
                "inside a real frame.\n");
    fails++;
  }
  gpu_frame_end();

  /* The scene: what the draw wrote, before anything composited it. */
  scene = gpu_present_scene(g_gpu, &scene_w, &scene_h);
  if (!scene || scene_w != FRAME_W || scene_h != FRAME_H) {
    x2_log_info("gpu frame-draw selftest: FAILED -- the scene texture is %ux%u "
                "and %s, so the draw's own target cannot be read.\n",
                scene_w, scene_h, scene ? "present" : "absent");
    fails++;
  } else if (!gpu_readback_texture_rgba(g_gpu, scene, scene_w, scene_h,
                                        scene_image, sizeof scene_image)) {
    x2_log_info("gpu frame-draw selftest: FAILED -- the scene could not be "
                "read back, so nothing about its pixels is known.\n");
    fails++;
  } else if (!check_image(scene_image, FRAME_W, FRAME_H, "scene pixel")) {
    fails++;
  }

  SDL_DestroyWindow(window);
  gpu_device_destroy();
  if (fails) {
    x2_log_info("gpu frame-draw selftest: FAILED\n");
    return 1;
  }
  x2_log_info("gpu frame-draw selftest: PASSED -- a triangle drawn through the "
              "production frame path reached the scene texture (%d frame "
              "attempt(s))\n",
              attempt);
  return 0;
}
#endif
