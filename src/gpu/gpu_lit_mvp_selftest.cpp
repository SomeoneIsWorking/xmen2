/* The one self-test that draws through the MVP + lighting vertex branch. */
#include "gpu_selftests.h"

#include "../native/x2_log.h"
#include "gpu_device.h"
#include "gpu_draw.h"
#include "gpu_selftest_pixels.h"

#include <string.h>

/*
 * Issue #152: every other draw self-test uses D3DFVF_XYZRHW
 * (`d.pretransformed = 1`), which takes the vertex shader's `vs.pretransformed`
 * branch and never reads `vs.mvp`, `vs.world`, or any of the lighting/material
 * fields declared after them in `VertexState` -- a block that interleaves bare
 * `uint` scalars between `mat4`/`vec4` members, hand-padded for std140. No
 * self-test on any backend had ever exercised that branch or those fields
 * before this one.
 *
 * This draws through the D3DFVF_XYZ (MVP) branch with fixed-function lighting
 * ON, an IDENTITY mvp/world (so the result does not also depend on getting a
 * non-trivial matrix's row/column-major convention right -- identity is its
 * own transpose, which isolates this test to "are the uniform fields read from
 * the right offsets" and nothing else), zero lights (so the only inputs are
 * material emissive/ambient/global-ambient, values that live in the same
 * scalar-interleaved region of the struct the pretransformed tests never
 * touch), and a known, non-trivial expected colour. A packing mismatch between
 * this struct's C/GLSL std140 layout and whatever layout a given SDL_GPU
 * backend's shader cross-compilation actually produces would read material
 * colour fields from the wrong byte offsets -- most likely producing a wrong
 * or degenerate MVP too, since it lives in the same buffer -- and this is
 * designed to catch either.
 */
int gpu_lit_mvp_selftest(void) {
#ifndef X2_WITH_SDL
  x2_log_info("gpu lit/mvp draw selftest: SKIPPED -- built without SDL. This "
              "is not a pass.\n");
  return 77;
#else
  /* NDC-space positions (no RHW): this is the D3DFVF_XYZ / vs.mvp branch, not
     the pretransformed one every earlier self-test used. */
  struct {
    float x, y, z;
  } tri[3] = {{0.0f, 0.8f, 0.5f}, {0.8f, -0.8f, 0.5f}, {-0.8f, -0.8f, 0.5f}};
  static const float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0,
                                     0, 0, 1, 0, 0, 0, 0, 1};
  static uint32_t img[OFF_W * OFF_H];
  GpuBuffer vb;
  GpuDraw d;
  int fails = 0;
  /* R=0.2 G=0.4 B=0.8, none near a rounding-tie byte boundary (0.5, 1.5, ...
     /255ths); a tolerant compare below allows +/-3/255 of legitimate
     backend-to-backend rounding difference while still failing hard on
     "black", "the clear colour", or genuinely garbage output. */
  const int want_r = 51, want_g = 102, want_b = 204, want_a = 255;

  x2_log_info("\n=== gpu lit/mvp draw selftest: the D3DFVF_XYZ + lighting "
              "branch, untouched by any earlier self-test ===\n");
  if (!gpu_device_create()) {
    x2_log_info("gpu lit/mvp draw selftest: FAILED -- no GPU device.\n");
    return 1;
  }
  vb = gpu_buffer_create(GPU_BUF_VERTEX, sizeof tri);
  if (!vb || !gpu_buffer_upload(vb, 0, tri, sizeof tri)) {
    x2_log_info("gpu lit/mvp draw selftest: FAILED -- the vertex buffer "
                "could not be made or filled.\n");
    gpu_device_destroy();
    return 1;
  }
  /* Cleared to opaque BLUE; the expected lit colour (~0.2, 0.4, 0.8) cannot
     be confused with it or with black. */
  if (!gpu_offscreen_begin(OFF_W, OFF_H, 0.0f, 0.0f, 1.0f, 1.0f)) {
    x2_log_info("gpu lit/mvp draw selftest: FAILED -- no off-screen "
                "target.\n");
    gpu_device_destroy();
    return 1;
  }

  memset(&d, 0, sizeof d);
  d.vertices = vb;
  d.vertex_stride = sizeof tri[0];
  d.prim = GPU_PRIM_TRIANGLELIST;
  d.prim_count = 1;
  d.pos_offset = 0;
  d.pretransformed = 0; /* the branch no earlier self-test reaches */
  d.color_offset = -1;
  d.specular_offset = -1;
  d.uv_offset = -1;
  d.normal_offset = -1;
  d.texop = GPU_TEXOP_NONE;
  d.cull = GPU_CULL_NONE;
  d.depth_func = GPU_CMP_ALWAYS;
  memcpy(d.mvp, identity, sizeof d.mvp);
  memcpy(d.world, identity, sizeof d.world);
  d.lighting = 1;
  d.color_vertex = 0;      /* material colours, not vertex ones -- there is no
                               vertex colour attribute here anyway */
  d.nlights = 0;           /* isolates this to material/global-ambient fields */
  d.mat_diffuse[3] = 1.0f; /* only .a is read when nlights == 0 */
  d.mat_emissive[0] = 0.2f;
  d.mat_emissive[1] = 0.4f;
  d.mat_emissive[2] = 0.8f;
  if (!gpu_draw(&d)) {
    x2_log_info("gpu lit/mvp draw selftest: FAILED -- the draw was "
                "refused.\n");
    gpu_offscreen_end();
    gpu_device_destroy();
    return 1;
  }
  if (!gpu_offscreen_read(img, sizeof img)) {
    x2_log_info("gpu lit/mvp draw selftest: FAILED -- the target could not "
                "be read back, so nothing about the pixels is known.\n");
    gpu_offscreen_end();
    gpu_device_destroy();
    return 1;
  }
  gpu_offscreen_end();

  {
    uint32_t px = img[(OFF_H / 2) * OFF_W + (OFF_W / 2)];
    int got_b = (int)(px & 0xffu);
    int got_g = (int)((px >> 8) & 0xffu);
    int got_r = (int)((px >> 16) & 0xffu);
    int got_a = (int)((px >> 24) & 0xffu);
    int dr = got_r - want_r, dg = got_g - want_g, db = got_b - want_b,
        da = got_a - want_a;
    if (dr < -3 || dr > 3 || dg < -3 || dg > 3 || db < -3 || db > 3 ||
        da < -3 || da > 3) {
      x2_log_error(
          "gpu lit/mvp draw selftest: FAILED -- centre pixel is "
          "0x%08x (a=%d r=%d g=%d b=%d), wanted ~0x%02x%02x%02x%02x "
          "(a=%d r=%d g=%d b=%d) within +/-3 -- the MVP+lighting branch of "
          "the fixed-function shader did not produce the material's "
          "emissive colour.\n",
          px, got_a, got_r, got_g, got_b, want_a, want_r, want_g, want_b,
          want_a, want_r, want_g, want_b);
      fails++;
    }
  }
  fails += !px_is(img, 1, 1, 0xFF0000FFu,
                  "a corner OUTSIDE the triangle, which must still be the "
                  "clear colour");

  gpu_draw_report();
  gpu_device_destroy();
  x2_log_info("gpu lit/mvp draw selftest: %s\n",
              fails
                  ? "FAILED"
                  : "PASSED -- an MVP-transformed, lit triangle read back its "
                    "material's emissive colour");
  return fails ? 1 : 0;
#endif
}
