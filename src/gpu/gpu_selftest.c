/* Asset-free pixel proofs for the host renderer's draw paths. */
#include "../native/x2_log.h"
#include "gpu_device.h"
#include "gpu_draw.h"
#include "gpu_selftest_pixels.h"

#include <stdio.h>
#include <string.h>

/* ---- the draw path, proved by reading the pixels back ------------------ */

/*
 * A frame path that presents and a frame path that DRAWS are different
 * claims, and the first has been true here for a while without the second.
 * So this does not check that gpu_draw returned 1 -- that is a mechanism
 * check. It renders into an off-screen target, copies it back, and looks at
 * the pixels.
 *
 * Designed around its NEGATIVE: the target is cleared to a colour that is not
 * the triangle's, so "the triangle is there" and "the clear is there" cannot
 * be confused, and the test fails if the centre is still the clear colour.
 * Two corners are checked too -- a shader that filled the whole target would
 * otherwise pass.
 */

/*
 * A clear that arrives AFTER drawing has begun.
 *
 * SDL_GPU clears on pass entry only, so this used to be counted and dropped --
 * 3,833 times in one gameplay run. The pass is now ended and reopened with the
 * clear as its load op, and the interesting claim is not "the clear happened"
 * but "the clear happened AND what was already drawn survived it". A reopen
 * that got the load ops wrong would erase the frame and still pass a test that
 * only checked the cleared thing.
 *
 * So: draw red, clear DEPTH ONLY, and require the red to still be there; then
 * clear COLOUR and require it to be gone. Either check alone is passed by a
 * wrong implementation -- the first by dropping the clear entirely (the old
 * behaviour), the second by restarting the frame.
 */
int gpu_midframe_clear_selftest(void) {
#ifndef X2_WITH_SDL
  x2_log_info(
      "gpu mid-frame clear selftest: SKIPPED -- built without SDL. This "
      "is not a pass.\n");
  return 77;
#else
  struct {
    float x, y, z, rhw;
    uint32_t color;
  } tri[3] = {{OFF_W * 0.5f, 2.0f, 0.5f, 1.0f, 0xFFFF0000u},
              {OFF_W - 2.0f, OFF_H - 2.0f, 0.5f, 1.0f, 0xFFFF0000u},
              {2.0f, OFF_H - 2.0f, 0.5f, 1.0f, 0xFFFF0000u}};
  static uint32_t img[OFF_W * OFF_H];
  GpuBuffer vb;
  GpuDraw d;
  int fails = 0;

  x2_log_info("\n=== gpu mid-frame clear selftest: the clear happens and the "
              "frame survives it ===\n");
  if (!gpu_device_create()) {
    x2_log_info("gpu mid-frame clear selftest: FAILED -- no GPU device.\n");
    return 1;
  }
  vb = gpu_buffer_create(GPU_BUF_VERTEX, sizeof tri);
  if (!vb || !gpu_buffer_upload(vb, 0, tri, sizeof tri)) {
    x2_log_info("gpu mid-frame clear selftest: FAILED -- no vertex buffer.\n");
    gpu_device_destroy();
    return 1;
  }
  memset(&d, 0, sizeof d);
  d.vertices = vb;
  d.vertex_stride = sizeof tri[0];
  d.prim = GPU_PRIM_TRIANGLELIST;
  d.prim_count = 1;
  d.pos_offset = 0;
  d.pretransformed = 1;
  d.color_offset = 16;
  d.uv_offset = -1;
  d.texop = GPU_TEXOP_NONE;
  d.cull = GPU_CULL_NONE;
  d.depth_func = GPU_CMP_ALWAYS;

  /* --- depth-only clear mid-frame: the red must survive --- */
  if (!gpu_offscreen_begin(OFF_W, OFF_H, 0.0f, 0.0f, 1.0f, 1.0f) ||
      !gpu_draw(&d)) {
    x2_log_info("gpu mid-frame clear selftest: FAILED -- the first draw did "
                "not happen, so nothing was compared.\n");
    gpu_offscreen_end();
    gpu_device_destroy();
    return 1;
  }
  gpu_frame_clear(2u, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0u); /* depth only */
  if (!gpu_offscreen_read(img, sizeof img)) {
    x2_log_info("gpu mid-frame clear selftest: FAILED -- no read back.\n");
    gpu_offscreen_end();
    gpu_device_destroy();
    return 1;
  }
  gpu_offscreen_end();
  fails += !px_is(img, OFF_W / 2, OFF_H / 2, 0xFFFF0000u,
                  "the triangle, which a DEPTH-only clear must not erase");
  fails += !px_is(img, 1, 1, 0xFF0000FFu,
                  "the background, which a depth-only clear must also "
                  "leave alone");

  /* --- colour clear mid-frame: the red must be gone --- */
  if (!gpu_offscreen_begin(OFF_W, OFF_H, 0.0f, 0.0f, 1.0f, 1.0f) ||
      !gpu_draw(&d)) {
    x2_log_info("gpu mid-frame clear selftest: FAILED -- the second draw did "
                "not happen.\n");
    gpu_offscreen_end();
    gpu_device_destroy();
    return 1;
  }
  gpu_frame_clear(1u, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0u); /* colour green */
  if (!gpu_offscreen_read(img, sizeof img)) {
    x2_log_info("gpu mid-frame clear selftest: FAILED -- no read back.\n");
    gpu_offscreen_end();
    gpu_device_destroy();
    return 1;
  }
  gpu_offscreen_end();
  fails += !px_is(img, OFF_W / 2, OFF_H / 2, 0xFF00FF00u,
                  "the middle after a mid-frame COLOUR clear to green -- "
                  "still red here means the clear was dropped");

  gpu_device_destroy();
  x2_log_info(
      "gpu mid-frame clear selftest: %s\n",
      fails ? "FAILED"
            : "PASSED -- a depth-only clear leaves the picture alone and a "
              "colour clear replaces it");
  return fails ? 1 : 0;
#endif
}

int gpu_draw_selftest(void) {
#ifndef X2_WITH_SDL
  x2_log_info("gpu draw selftest: SKIPPED -- built without SDL. This is not a "
              "pass.\n");
  return 77;
#else
  /* A big clockwise triangle covering the middle, pre-transformed so no
     matrix is involved: this is testing the draw path, not the maths. */
  struct {
    float x, y, z, rhw;
    uint32_t color;
  } tri[3] = {{OFF_W * 0.5f, 2.0f, 0.0f, 1.0f, 0xFFFF0000u},
              {OFF_W - 2.0f, OFF_H - 2.0f, 0.0f, 1.0f, 0xFFFF0000u},
              {2.0f, OFF_H - 2.0f, 0.0f, 1.0f, 0xFFFF0000u}};
  static uint32_t img[OFF_W * OFF_H];
  GpuBuffer vb;
  GpuDraw d;
  int fails = 0;

  x2_log_info(
      "\n=== gpu draw selftest: geometry, with no engine involved ===\n");
  if (!gpu_device_create()) {
    x2_log_info("gpu draw selftest: FAILED -- no GPU device.\n");
    return 1;
  }
  vb = gpu_buffer_create(GPU_BUF_VERTEX, sizeof tri);
  if (!vb || !gpu_buffer_upload(vb, 0, tri, sizeof tri)) {
    x2_log_info("gpu draw selftest: FAILED -- the vertex buffer could not be "
                "made or filled.\n");
    gpu_device_destroy();
    return 1;
  }
  /* Cleared to opaque BLUE; the triangle is opaque RED. Neither can be
     mistaken for the other, or for uninitialised memory. */
  if (!gpu_offscreen_begin(OFF_W, OFF_H, 0.0f, 0.0f, 1.0f, 1.0f)) {
    x2_log_info("gpu draw selftest: FAILED -- no off-screen target.\n");
    gpu_device_destroy();
    return 1;
  }

  memset(&d, 0, sizeof d);
  d.vertices = vb;
  d.vertex_stride = sizeof tri[0];
  d.prim = GPU_PRIM_TRIANGLELIST;
  d.prim_count = 1;
  d.pos_offset = 0;
  d.pretransformed = 1;
  d.color_offset = 16;
  d.uv_offset = -1;
  d.texop = GPU_TEXOP_NONE;
  d.cull = GPU_CULL_NONE;
  d.depth_func = GPU_CMP_ALWAYS;
  if (!gpu_draw(&d)) {
    x2_log_info("gpu draw selftest: FAILED -- the draw was refused.\n");
    gpu_offscreen_end();
    gpu_device_destroy();
    return 1;
  }
  if (!gpu_offscreen_read(img, sizeof img)) {
    x2_log_info("gpu draw selftest: FAILED -- the target could not be read "
                "back, so nothing about the pixels is known.\n");
    gpu_offscreen_end();
    gpu_device_destroy();
    return 1;
  }
  gpu_offscreen_end();

  /* B8G8R8A8 in memory, read as a little-endian uint32 -> 0xAARRGGBB. */
  fails += !px_is(img, OFF_W / 2, OFF_H / 2, 0xFFFF0000u,
                  "the middle of the triangle");
  fails += !px_is(img, 1, 1, 0xFF0000FFu,
                  "a corner OUTSIDE the triangle, which must still be the "
                  "clear colour");
  fails += !px_is(img, OFF_W - 2, 1, 0xFF0000FFu,
                  "the other top corner, outside the triangle");

  gpu_draw_report();
  gpu_device_destroy();
  x2_log_info("gpu draw selftest: %s\n", fails
                                             ? "FAILED"
                                             : "PASSED -- a triangle "
                                               "was rasterised and read back");
  return fails ? 1 : 0;
#endif
}

/*
 * Issue #152: every draw self-test above uses D3DFVF_XYZRHW
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
  d.color_vertex = 0; /* material colours, not vertex ones -- there is no
                          vertex colour attribute here anyway */
  d.nlights = 0;       /* isolates this to material/global-ambient fields */
  d.mat_diffuse[3] = 1.0f;   /* only .a is read when nlights == 0 */
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
  x2_log_info(
      "gpu lit/mvp draw selftest: %s\n",
      fails ? "FAILED"
            : "PASSED -- an MVP-transformed, lit triangle read back its "
              "material's emissive colour");
  return fails ? 1 : 0;
#endif
}
