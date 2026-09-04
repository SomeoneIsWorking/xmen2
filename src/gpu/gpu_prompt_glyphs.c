#include "../native/x2_log.h"
/*
 * Native prompt glyph drawing, independent of the guest D3D8 objects.
 *
 * The text engine still owns layout. prompt_glyph_draw.c captures its local
 * rectangle and batch colour; this owner retains the port's RGBA SVG atlas,
 * turns each rectangle into two triangles in the stock (x, 0, y) text plane,
 * and submits them through the host GPU. The engine draw bridge supplies the
 * text batch's finalized matrix when it calls this module, so this owner
 * contains no guest addresses or CPU state.
 */
#include "gpu_prompt_glyphs.h"

#include "gpu_device.h"
#include "gpu_draw.h"
#include "prompt_glyph_atlas.h"
#include "prompt_glyph_quads.h"

#include <stdio.h>
#include <string.h>

struct PromptVertex {
  float x, y, z;
  uint32_t color;
  float u, v;
};

static GpuTexture g_atlas;
static GpuBuffer g_vertices;
static unsigned long g_frames_ready, g_render_calls, g_drawn, g_refused;

static int ensure_resources(void) {
  if (g_atlas && g_vertices)
    return 1;
  if (!gpu_device_ready())
    return 0;
  g_atlas = gpu_texture_create(X2_PROMPT_ATLAS_W, X2_PROMPT_ATLAS_H,
                               GPU_FMT_RGBA8, 1);
  if (g_atlas &&
      !gpu_texture_upload(g_atlas, 0, x2_prompt_atlas, X2_PROMPT_ATLAS_BYTES)) {
    gpu_texture_destroy(g_atlas);
    g_atlas = 0;
  }
  g_vertices = gpu_buffer_create(GPU_BUF_VERTEX,
                                 X2_PROMPT_QUADS_MAX * 6u *
                                     (uint32_t)sizeof(struct PromptVertex));
  if (!g_atlas || !g_vertices) {
    if (g_vertices)
      gpu_buffer_destroy(g_vertices);
    if (g_atlas)
      gpu_texture_destroy(g_atlas);
    g_vertices = 0;
    g_atlas = 0;
    x2_log_error("prompt GPU: atlas or vertex-buffer creation failed; "
                 "native prompt batches will be refused.\n");
    return 0;
  }
  return 1;
}

static void write_quad(struct PromptVertex *v, const struct X2PromptQuad *q) {
  /* Generated UVs follow D3D's bottom-origin convention. SDL_GPU samples
     the top-down RGBA byte image, so the conversion belongs exactly here. */
  float top = 1.0f - q->v1;
  float bottom = 1.0f - q->v0;
  const struct PromptVertex corners[4] = {
      {q->x0, 0.0f, q->y0, q->color, q->u0, top},
      {q->x1, 0.0f, q->y0, q->color, q->u1, top},
      {q->x1, 0.0f, q->y1, q->color, q->u1, bottom},
      {q->x0, 0.0f, q->y1, q->color, q->u0, bottom},
  };
  v[0] = corners[0];
  v[1] = corners[1];
  v[2] = corners[2];
  v[3] = corners[0];
  v[4] = corners[2];
  v[5] = corners[3];
}

void gpu_prompt_glyphs_frame_begin(void) {
  x2_prompt_quads_reset();
  if (ensure_resources())
    g_frames_ready++;
}

int gpu_prompt_glyphs_render(const float mvp[16]) {
  struct PromptVertex vertices[X2_PROMPT_QUADS_MAX * 6u];
  const struct X2PromptQuad *quads;
  GpuDraw draw;
  unsigned count, i;

  g_render_calls++;
  quads = x2_prompt_quads(&count);
  if (!count)
    return 1;
  if (!mvp || !g_atlas || !g_vertices || !gpu_frame_in_progress()) {
    g_refused += count;
    return 0;
  }
  for (i = 0; i < count; i++)
    write_quad(&vertices[i * 6u], &quads[i]);
  if (!gpu_buffer_upload(g_vertices, 0, vertices,
                         count * 6u * (uint32_t)sizeof vertices[0])) {
    g_refused += count;
    return 0;
  }

  memset(&draw, 0, sizeof draw);
  draw.vertices = g_vertices;
  draw.vertex_stride = sizeof vertices[0];
  draw.prim = GPU_PRIM_TRIANGLELIST;
  draw.pos_offset = 0;
  draw.pretransformed = 0;
  draw.color_offset = 12;
  draw.uv_offset = 16;
  draw.normal_offset = -1;
  draw.texture = g_atlas;
  draw.texop = GPU_TEXOP_MODULATE;
  draw.alpha_op = GPU_TEXOP_MODULATE;
  draw.texture_clamp = 1;
  draw.blend_enable = 1;
  draw.src_blend = GPU_BLEND_SRCALPHA;
  draw.dst_blend = GPU_BLEND_INVSRCALPHA;
  draw.depth_func = GPU_CMP_ALWAYS;
  draw.cull = GPU_CULL_NONE;
  draw.first_vertex = 0;
  draw.prim_count = count * 2u;
  memcpy(draw.mvp, mvp, sizeof draw.mvp);
  if (!gpu_draw(&draw)) {
    g_refused += count;
    return 0;
  }
  g_drawn += count;
  x2_prompt_quads_consume();
  return 1;
}

void gpu_prompt_glyphs_shutdown(void) {
  if (g_vertices)
    gpu_buffer_destroy(g_vertices);
  if (g_atlas)
    gpu_texture_destroy(g_atlas);
  g_vertices = 0;
  g_atlas = 0;
}

void gpu_prompt_glyphs_report(void) {
  x2_log_info("  Prompt GPU: %lu text-boundary call(s), %lu glyph quad(s) "
              "submitted, "
              "%lu refused; resources ready at %lu frame begin(s)\n",
              g_render_calls, g_drawn, g_refused, g_frames_ready);
  if (!g_render_calls)
    x2_log_info("        ZERO text-boundary calls -- the engine override "
                "never ran; this says nothing about atlas pixels.\n");
}

int gpu_prompt_glyphs_selftest(void) {
#ifndef X2_WITH_SDL
  x2_log_info("prompt GPU selftest: SKIPPED -- built without SDL.\n");
  return 77;
#else
  static uint32_t pixels[96u * 64u];
  /* Row-vector matrix for the same (x, 0, y) plane used by stock text. */
  static const float pixel_mvp[16] = {
      2.0f / 96.0f, 0.0f,          0.0f, 0.0f, 0.0f,  1.0f, 0.0f, 0.0f,
      0.0f,         -2.0f / 64.0f, 1.0f, 0.0f, -1.0f, 1.0f, 0.0f, 1.0f,
  };
  const struct x2_prompt_cell *cell = &x2_prompt_cells[1]; /* red face B */
  struct X2PromptQuad full = {
      4.0f, 12.0f, 44.0f, 52.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0xffffffffu, 0x81u,
  };
  struct X2PromptQuad half = full;
  struct PromptVertex check[6];
  unsigned i, changed = 0;
  unsigned full_red = 0, half_red = 0;
  int failed = 0;

  x2_log_info(
      "\n=== prompt GPU selftest: RGBA atlas, V orientation and alpha ===\n");
  full.u0 = cell->u0;
  full.v0 = cell->v0;
  full.u1 = cell->u1;
  full.v1 = cell->v1;
  half = full;
  half.x0 = 52.0f;
  half.x1 = 92.0f;
  half.color = 0x80ffffffu;
  write_quad(check, &full);
  if (check[0].v != 1.0f - cell->v1 || check[2].v != 1.0f - cell->v0) {
    x2_log_info("prompt GPU selftest: FAILED -- bottom-origin atlas UVs were "
                "not flipped at the native sampler boundary.\n");
    return 1;
  }
  if (!gpu_device_create())
    return 1;
  gpu_prompt_glyphs_frame_begin();
  if (!x2_prompt_quads_add(&full) || !x2_prompt_quads_add(&half) ||
      !gpu_offscreen_begin(96, 64, 0.0f, 0.0f, 1.0f, 1.0f) ||
      !gpu_prompt_glyphs_render(pixel_mvp) ||
      !gpu_offscreen_read(pixels, sizeof pixels)) {
    x2_log_info("prompt GPU selftest: FAILED -- the production atlas draw "
                "could not be read back.\n");
    gpu_offscreen_end();
    gpu_device_destroy();
    return 1;
  }
  gpu_offscreen_end();
  for (i = 0; i < 96u * 64u; i++) {
    uint32_t p = pixels[i];
    unsigned x = i % 96u;
    unsigned red = (p >> 16) & 0xffu;
    if (p != 0xff0000ffu)
      changed++;
    if (x < 48u && red > full_red)
      full_red = red;
    if (x >= 48u && red > half_red)
      half_red = red;
  }
  if (pixels[0] != 0xff0000ffu || changed < 100u) {
    x2_log_info("prompt GPU selftest: FAILED -- transparent atlas pixels did "
                "not preserve the blue background (%u pixels changed).\n",
                changed);
    failed = 1;
  }
  if (full_red < 180u || half_red < 60u || full_red <= half_red + 30u) {
    x2_log_info("prompt GPU selftest: FAILED -- RGBA red/alpha did not survive "
                "the production path (full R=%u, half R=%u).\n",
                full_red, half_red);
    failed = 1;
  }
  gpu_device_destroy();
  x2_log_info("prompt GPU selftest: %s -- %u non-background pixels; full R=%u, "
              "half R=%u.\n",
              failed ? "FAILED" : "PASS", changed, full_red, half_red);
  return failed;
#endif
}
