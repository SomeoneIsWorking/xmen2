#include "../native/x2_log.h"
#include "gpu_device.h"
#include "gpu_draw.h"
#include "gpu_selftests.h"
#include "gpu_shadow.h"

#include <stdio.h>
#include <string.h>

#define SHADOW_TEST_W 64
#define SHADOW_TEST_H 64

#ifdef X2_WITH_SDL
typedef struct {
  float position[3];
  float normal[3];
} ShadowTestVertex;

static void identity(float matrix[16]) {
  memset(matrix, 0, sizeof(float) * 16);
  matrix[0] = matrix[5] = matrix[10] = matrix[15] = 1.0f;
}

static void describe_draw(GpuDraw *draw, GpuBuffer vertices,
                          unsigned triangles) {
  memset(draw, 0, sizeof *draw);
  draw->vertices = vertices;
  draw->vertex_stride = sizeof(ShadowTestVertex);
  draw->pos_offset = 0;
  draw->normal_offset = 12;
  draw->color_offset = -1;
  draw->specular_offset = -1;
  draw->uv_offset = -1;
  draw->prim = GPU_PRIM_TRIANGLELIST;
  draw->prim_count = triangles;
  draw->texop = GPU_TEXOP_NONE;
  draw->depth_test = 1;
  draw->depth_write = 1;
  draw->depth_func = GPU_CMP_LESSEQUAL;
  draw->cull = GPU_CULL_NONE;
  draw->lighting = 1;
  draw->nlights = 1;
  draw->mat_diffuse[0] = draw->mat_diffuse[1] = 1.0f;
  draw->mat_diffuse[2] = draw->mat_diffuse[3] = 1.0f;
  draw->light[0].type = GPU_LIGHT_DIRECTIONAL;
  draw->light[0].diffuse[0] = draw->light[0].diffuse[1] = 1.0f;
  draw->light[0].diffuse[2] = draw->light[0].diffuse[3] = 1.0f;
  draw->light[0].direction[1] = -1.0f;
  draw->light[0].direction[2] = 1.0f;
  identity(draw->world);
  identity(draw->mvp);
}

static int render_answer(int enabled, int include_caster, GpuDraw *caster,
                         GpuDraw *receiver, uint32_t *pixels) {
  gpu_shadow_configure(enabled, 512);
  if (!gpu_offscreen_begin(SHADOW_TEST_W, SHADOW_TEST_H, 0.0f, 0.0f, 0.0f,
                           1.0f))
    return 0;
  if ((include_caster && !gpu_draw(caster)) || !gpu_draw(receiver) ||
      !gpu_offscreen_read(pixels,
                          SHADOW_TEST_W * SHADOW_TEST_H * sizeof *pixels)) {
    gpu_offscreen_end();
    return 0;
  }
  gpu_offscreen_end();
  return 1;
}

/* A second shadow pass drawn with the pipeline the first one bound: the
   shadow pass's bind record must start empty with each pass, or this pass
   draws with no pipeline bound. */
static uint32_t g_second_pass[SHADOW_TEST_W * SHADOW_TEST_H];

static unsigned rgb_sum(uint32_t pixel) {
  return (pixel & 0xffu) + ((pixel >> 8) & 0xffu) + ((pixel >> 16) & 0xffu);
}

/*
 * A caster whose position a VS 1.1 program produces casts through
 * shadow_vs11.vert (issue #187). `mov oPos, v0` places it where the fixed
 * stage's identity transform does. Neither caster has a normal, so both are lit
 * alike, and the two images must be identical -- and must hold a shadow, or
 * they agree about nothing. The receiver is drawn first: a programmable draw
 * cannot set the frame's light, and the product's frames have a lit
 * fixed-function draw ahead of their skinned casters in the same way.
 */
static GpuVsProgram g_caster_program;
static float g_caster_constants[GPU_VS_CONSTANTS][4];
static uint32_t g_fixed_caster[SHADOW_TEST_W * SHADOW_TEST_H];
static uint32_t g_program_caster[SHADOW_TEST_W * SHADOW_TEST_H];

static int render_receiver_first(GpuDraw *receiver, GpuDraw *caster,
                                 uint32_t *pixels) {
  int ok;
  gpu_shadow_configure(1, 512);
  if (!gpu_offscreen_begin(SHADOW_TEST_W, SHADOW_TEST_H, 0.0f, 0.0f, 0.0f,
                           1.0f))
    return 0;
  ok = gpu_draw(receiver) && gpu_draw(caster) &&
       gpu_offscreen_read(pixels,
                          SHADOW_TEST_W * SHADOW_TEST_H * sizeof *pixels);
  gpu_offscreen_end();
  return ok;
}

static void describe_program(void) {
  const uint32_t position =
      (uint32_t)GPU_VS_REG_INPUT | 0xe4u << GPU_VS_SOURCE_SWIZZLE_SHIFT;
  memset(&g_caster_program, 0, sizeof g_caster_program);
  gpu_vs_program_set_input(&g_caster_program, 0, GPU_VS_INPUT_FLOAT3, 0);
  g_caster_program.block.insn[0][0] =
      (uint32_t)GPU_VS_OP_MOV | 0xfu << 8 | (uint32_t)GPU_VS_REG_OUT_POS << 16;
  g_caster_program.block.insn[0][1] = position;
  g_caster_program.block.insn[0][2] = position;
  g_caster_program.block.insn[0][3] = position;
  g_caster_program.block.count = 1;
}

static int program_caster_check(GpuBuffer caster_buffer, GpuDraw *receiver,
                                const uint32_t *no_caster) {
  GpuDraw fixed, program;
  unsigned shadowed = 0, differ = 0;

  describe_draw(&fixed, caster_buffer, 2);
  fixed.normal_offset = -1;
  program = fixed;
  describe_program();
  program.vs_program = &g_caster_program;
  program.vs_constants = g_caster_constants;
  program.programmable = 1;
  program.color_offset = 0;
  program.uv_offset = 0;
  if (!render_receiver_first(receiver, &fixed, g_fixed_caster) ||
      !render_receiver_first(receiver, &program, g_program_caster)) {
    x2_log_info("gpu shadow selftest: FAILED -- the VS 1.1 caster could not "
                "be drawn.\n");
    return 1;
  }
  for (unsigned i = 0; i < SHADOW_TEST_W * SHADOW_TEST_H; i++) {
    if (rgb_sum(g_fixed_caster[i]) + 60 < rgb_sum(no_caster[i]))
      shadowed++;
    if (g_fixed_caster[i] != g_program_caster[i])
      differ++;
  }
  if (shadowed < 20 || differ) {
    x2_log_info("gpu shadow selftest: FAILED -- the fixed caster shadowed %u "
                "pixels; the VS 1.1 caster's image differs from it in %u.\n",
                shadowed, differ);
    return 1;
  }
  return 0;
}
#endif

int gpu_shadow_selftest(void) {
#ifndef X2_WITH_SDL
  x2_log_info(
      "gpu shadow selftest: SKIPPED -- built without SDL. This is not a "
      "pass.\n");
  return 77;
#else
  static const ShadowTestVertex caster_vertices[6] = {
      {{-.20f, .20f, .30f}, {0.f, .7071f, -.7071f}},
      {{.20f, .20f, .30f}, {0.f, .7071f, -.7071f}},
      {{.20f, .60f, .30f}, {0.f, .7071f, -.7071f}},
      {{-.20f, .20f, .30f}, {0.f, .7071f, -.7071f}},
      {{.20f, .60f, .30f}, {0.f, .7071f, -.7071f}},
      {{-.20f, .60f, .30f}, {0.f, .7071f, -.7071f}},
  };
  static const ShadowTestVertex receiver_vertices[6] = {
      {{-.90f, -.80f, .80f}, {0.f, .7071f, -.7071f}},
      {{.90f, -.80f, .80f}, {0.f, .7071f, -.7071f}},
      {{.90f, .80f, .80f}, {0.f, .7071f, -.7071f}},
      {{-.90f, -.80f, .80f}, {0.f, .7071f, -.7071f}},
      {{.90f, .80f, .80f}, {0.f, .7071f, -.7071f}},
      {{-.90f, .80f, .80f}, {0.f, .7071f, -.7071f}},
  };
  static uint32_t disabled[SHADOW_TEST_W * SHADOW_TEST_H];
  static uint32_t enabled[SHADOW_TEST_W * SHADOW_TEST_H];
  static uint32_t no_caster[SHADOW_TEST_W * SHADOW_TEST_H];
  GpuBuffer caster_buffer, receiver_buffer;
  GpuDraw caster, receiver;
  unsigned darker = 0, caster_dependent = 0, unchanged = 0;
  int result = 1;

  x2_log_info("\n=== gpu shadow selftest: sampled light-depth occlusion ===\n");
  if (!gpu_device_create())
    return 1;
  caster_buffer = gpu_buffer_create(GPU_BUF_VERTEX, sizeof caster_vertices);
  receiver_buffer = gpu_buffer_create(GPU_BUF_VERTEX, sizeof receiver_vertices);
  if (!caster_buffer || !receiver_buffer ||
      !gpu_buffer_upload(caster_buffer, 0, caster_vertices,
                         sizeof caster_vertices) ||
      !gpu_buffer_upload(receiver_buffer, 0, receiver_vertices,
                         sizeof receiver_vertices))
    goto done;
  describe_draw(&caster, caster_buffer, 2);
  describe_draw(&receiver, receiver_buffer, 2);
  if (!render_answer(0, 1, &caster, &receiver, disabled) ||
      !render_answer(1, 1, &caster, &receiver, enabled) ||
      !render_answer(1, 0, &caster, &receiver, no_caster) ||
      !render_answer(1, 1, &caster, &receiver, g_second_pass))
    goto done;
  if (memcmp(g_second_pass, enabled, sizeof enabled) != 0) {
    x2_log_info("gpu shadow selftest: FAILED -- a second shadow pass with the "
                "same caster drew a different image from the first.\n");
    goto done;
  }

  for (unsigned i = 0; i < SHADOW_TEST_W * SHADOW_TEST_H; i++) {
    unsigned off = rgb_sum(disabled[i]);
    unsigned on = rgb_sum(enabled[i]);
    unsigned open = rgb_sum(no_caster[i]);
    if (on + 60 < off)
      darker++;
    if (on + 60 < open)
      caster_dependent++;
    if (on == off)
      unchanged++;
  }
  if (darker < 20 || caster_dependent < 20 || unchanged < 1000) {
    x2_log_info("gpu shadow selftest: FAILED -- %u pixels darkened with the "
                "pass, %u depended on the caster, %u stayed unchanged. The "
                "test requires all three answers.\n",
                darker, caster_dependent, unchanged);
    goto done;
  }
  if (program_caster_check(caster_buffer, &receiver, no_caster))
    goto done;
  x2_log_info(
      "gpu shadow selftest: PASSED -- %u pixels darkened only when the "
      "shadow pass was sampled; %u vanished when the caster was removed; "
      "%u control pixels stayed unchanged; a VS 1.1 caster shadowed the "
      "same pixels.\n",
      darker, caster_dependent, unchanged);
  result = 0;

done:
  if (caster_buffer)
    gpu_buffer_destroy(caster_buffer);
  if (receiver_buffer)
    gpu_buffer_destroy(receiver_buffer);
  gpu_device_destroy();
  return result;
#endif
}
