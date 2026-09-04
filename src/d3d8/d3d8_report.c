#include "../native/x2_log.h"
/*
 * What the layer says about itself at shutdown, and what it proves about
 * itself on demand.
 *
 * Kept apart from the interfaces on purpose: the report is what a reader sees
 * when nothing was drawn, and it has to be able to distinguish "the engine
 * never reached DirectX" from "it reached it and this host did nothing" from
 * "it drew and the frame was empty". Those three look identical on a black
 * screen and are three completely different bugs.
 */
#include "d3d8_caps.h"
#include "d3d8_com.h"
#include "d3d8_device.h"
#include "d3d8_drawcall.h"
#include "d3d8_host.h"
#include "d3d8_light_selftest.h"
#include "d3d8_resource.h"
#include "d3d8_screen_space_test.h"
#include "d3d8_state.h"
#include "d3d8_surface.h"
#include "d3d8_types.h"
#include "d3d8_vertex_shader.h"

#include "gpu_device.h"
#include "gpu_draw.h"
#include "guest_heap.h"
#include "guest_memory.h"
#include "x86rt.h"

#include <stdio.h>
#include <string.h>
/*
 * The caps block's own check.
 *
 * Not "does it look right" -- that cannot fail. It checks the two things that
 * would be silently wrong: that the struct this host writes is the size the
 * guest allocated for it (the game allocates 0xd4 and would be overrun by a
 * byte more), and that the fields land where a D3D8 caller reads them, tested
 * through the offsets rather than the field names.
 */
static int caps_selftest(void) {
  D3DCAPS8 c;
  D3D8CapsLimits hw;
  const uint32_t *raw = (const uint32_t *)&c;
  int fails = 0;

  d3d8_caps_limits_default(&hw);
  memset(&c, 0xAA, sizeof c);
  d3d8_caps_fill(&c, 0, D3DDEVTYPE_HAL, &hw);

  if (sizeof c != 212) {
    x2_log_error("  FAIL: D3DCAPS8 is %d bytes; the game allocates "
                 "212\n",
                 (int)sizeof c);
    fails++;
  }
  /* DeviceType is dword 0 and MaxTextureWidth is dword 22; reading them
     positionally is what catches a field inserted in the wrong place. */
  if (raw[0] != D3DDEVTYPE_HAL) {
    x2_log_error("  FAIL: dword 0 is 0x%08x, not the device type\n", raw[0]);
    fails++;
  }
  if (raw[22] != hw.max_texture_dim) {
    x2_log_error("  FAIL: dword 22 is 0x%08x, not MaxTextureWidth "
                 "(%u)\n",
                 raw[22], hw.max_texture_dim);
    fails++;
  }
  if (raw[49] != 0xFFFE0101u) {
    x2_log_error("  FAIL: dword 49 is 0x%08x, not VertexShaderVersion "
                 "1.1\n",
                 raw[49]);
    fails++;
  }
  /* Nothing may be left as the 0xAA fill: a field the filler forgot is a
     field the engine reads as garbage. memset(0) first would have hidden
     exactly that, which is why the fill is 0xAA. */
  {
    int i, untouched = 0;
    for (i = 0; i < (int)(sizeof c / 4); i++)
      if (raw[i] == 0xAAAAAAAAu)
        untouched++;
    if (untouched) {
      x2_log_error("  FAIL: %d caps dword(s) were never written by "
                   "d3d8_caps_fill\n",
                   untouched);
      fails++;
    }
  }
  x2_log_info("d3d8: caps self-test %s\n", fails ? "FAILED" : "passed");
  return fails;
}

/*
 * The D3D8 half of the draw path, driven THROUGH THE COM VTABLES.
 *
 * Not by calling the C functions directly: the guest reaches this layer by
 * dispatching through a vtable slot, and that is the path that has to work.
 * So the buffer is created, locked, written and unlocked exactly as libIGGfx
 * would, and only then is a draw built from the device state and rasterised
 * into an off-screen target and read back.
 *
 * Its negative is designed first: the target is cleared GREEN and the triangle
 * is RED, so "the triangle drew" cannot be confused with "the clear drew", and
 * a corner outside the triangle is checked so a shader that filled everything
 * would fail.
 */
static uint32_t call_method(D3D8Object *o, int slot, const uint32_t *args,
                            int nargs) {
  CPU C;
  static uint32_t stack;
  uint32_t vt = d3d8_iface_vtable(d3d8_object_iface(o));
  int i;

  if (!stack)
    stack = guest_malloc(4096) + 2048;
  cpu_reset(&C);
  C.reg[kX86pEsp] = stack - (uint32_t)(nargs + 2) * 4u;
  WR32(C.reg[kX86pEsp], 0xD3D80000u);               /* return address */
  WR32(C.reg[kX86pEsp] + 4u, d3d8_object_guest(o)); /* this */
  for (i = 0; i < nargs; i++)
    WR32(C.reg[kX86pEsp] + 8u + (uint32_t)i * 4u, args[i]);
  x86_dispatch(&C, RD32(vt + (uint32_t)slot * 4u));
  return C.reg[kX86pEax];
}

#define TW 64
#define TH 64

static int d3d8_draw_selftest(void) {
  /* D3DFVF_XYZRHW | D3DFVF_DIFFUSE, and D3DCOLOR is 0xAARRGGBB. */
  struct {
    float x, y, z, rhw;
    uint32_t color;
  } tri[3] = {{TW * 0.5f, 2.0f, 0.0f, 1.0f, 0xFFFF0000u},
              {TW - 2.0f, TH - 2.0f, 0.0f, 1.0f, 0xFFFF0000u},
              {2.0f, TH - 2.0f, 0.0f, 1.0f, 0xFFFF0000u}};
  struct {
    float x, y, z;
    uint32_t color;
  } model_tri[3] = {{0.0f, 0.8f, 0.0f, 0xFFFF0000u},
                    {-0.8f, -0.8f, 0.0f, 0xFFFF0000u},
                    {0.8f, -0.8f, 0.0f, 0xFFFF0000u}};
  static uint32_t img[TW * TH];
  D3D8Object *vb, *model_vb;
  D3D8State st;
  D3D8DrawRequest req;
  GpuDraw gd;
  uint32_t args[3], locked = 0;
  int fails = 0;

  x2_log_info("\n=== d3d8 draw selftest: through the COM vtables ===\n");
  if (!gpu_device_create()) {
    x2_log_info("d3d8 draw selftest: FAILED -- no GPU device.\n");
    return 1;
  }
  d3d8_resource_install();
  vb = d3d8_vertexbuffer_new(sizeof tri, 0, 0x0044u /* XYZRHW|DIFFUSE */, 0);
  if (!vb) {
    x2_log_info("d3d8 draw selftest: FAILED -- no vertex buffer.\n");
    gpu_device_destroy();
    return 1;
  }
  d3d8_resource_attach_destructor(vb);

  /* Lock(offset, size, &pointer, flags) -- slot 11 of IDirect3DVertexBuffer8.
   */
  args[0] = 0;
  args[1] = 0;
  args[2] = guest_malloc(4);
  call_method(vb, 11, args, 4);
  locked = RD32(args[2]);
  if (!locked) {
    x2_log_info("d3d8 draw selftest: FAILED -- Lock returned no pointer.\n");
    gpu_device_destroy();
    return 1;
  }
  memcpy(guest_memory_pointer(locked), tri, sizeof tri);
  call_method(vb, 12, NULL, 0); /* Unlock */

  /* The device state the engine would have set. */
  d3d8_state_reset(&st);
  st.vertex_shader = 0x0044u;
  st.stream[0].guest_ptr = d3d8_object_guest(vb);
  st.stream[0].stride = sizeof tri[0];
  /* Deliberately left at D3D's DEFAULT cull mode (D3DCULL_CCW) rather than
     disabled: the triangle is wound the way the engine winds a front face,
     so this test is also what catches the front-face convention being
     inverted -- which it was. */

  memset(&req, 0, sizeof req);
  req.vertex_buffer = d3d8_resource_buffer(vb);
  req.stride = sizeof tri[0];
  /* The range check reads this: a request that leaves it 0 is saying
     the stream holds no vertices, and is refused -- correctly. The
     real path always sets it from the resource. */
  req.vertex_bytes = sizeof tri;
  req.primitive_type = 4; /* D3DPT_TRIANGLELIST */
  req.primitive_count = 1;
  if (!d3d8_build_draw(&st, &req, &gd)) {
    x2_log_info("d3d8 draw selftest: FAILED -- the draw could not be built "
                "from the device state.\n");
    gpu_device_destroy();
    return 1;
  }
  if (!gd.pretransformed || gd.color_offset != 16 || gd.pos_offset != 0) {
    x2_log_info("d3d8 draw selftest: FAILED -- FVF 0x0044 decoded to "
                "pos=%d colour=%d pretransformed=%d; expected 0, 16, 1.\n",
                gd.pos_offset, gd.color_offset, gd.pretransformed);
    fails++;
  }
  if (!gpu_offscreen_begin(TW, TH, 0.0f, 1.0f, 0.0f, 1.0f)) {
    x2_log_info("d3d8 draw selftest: FAILED -- no off-screen target.\n");
    gpu_device_destroy();
    return 1;
  }
  if (!gpu_draw(&gd)) {
    x2_log_info("d3d8 draw selftest: FAILED -- the draw was refused.\n");
    gpu_offscreen_end();
    gpu_device_destroy();
    return 1;
  }
  if (!gpu_offscreen_read(img, sizeof img)) {
    x2_log_info("d3d8 draw selftest: FAILED -- nothing could be read back, so "
                "nothing about the pixels is known.\n");
    gpu_offscreen_end();
    gpu_device_destroy();
    return 1;
  }
  gpu_offscreen_end();

  fails += d3d8_screen_space_pixels_check(img, TW, TH);

  /* Model-space positions take a different Y/winding route from XYZRHW.
     This is the D3DCULL_CW half the screen-space test above cannot see. */
  model_vb =
      d3d8_vertexbuffer_new(sizeof model_tri, 0, 0x0042u /* XYZ|DIFFUSE */, 0);
  locked = 0;
  if (model_vb) {
    args[0] = 0;
    args[1] = 0;
    args[2] = guest_malloc(4);
    call_method(model_vb, 11, args, 4);
    locked = RD32(args[2]);
  }
  if (!locked) {
    x2_log_info("d3d8 draw selftest: FAILED -- model-space cull control could "
                "not be populated.\n");
    fails++;
  } else {
    memcpy(guest_memory_pointer(locked), model_tri, sizeof model_tri);
    call_method(model_vb, 12, NULL, 0);
    d3d8_state_reset(&st);
    st.vertex_shader = 0x0042u;
    st.stream[0].guest_ptr = d3d8_object_guest(model_vb);
    st.stream[0].stride = sizeof model_tri[0];
    d3d8_state_set_render(&st, 22, 2); /* D3DCULL_CW */
    memset(&req, 0, sizeof req);
    req.vertex_buffer = d3d8_resource_buffer(model_vb);
    req.stride = sizeof model_tri[0];
    req.vertex_bytes = sizeof model_tri;
    req.primitive_type = 4;
    req.primitive_count = 1;
    if (!gpu_offscreen_begin(TW, TH, 0.0f, 1.0f, 0.0f, 1.0f) ||
        !d3d8_build_draw(&st, &req, &gd) || !gpu_draw(&gd) ||
        !gpu_offscreen_read(img, sizeof img)) {
      x2_log_info("d3d8 draw selftest: FAILED -- model-space D3DCULL_CW "
                  "control did not execute.\n");
      fails++;
    } else if (img[(TH / 2) * TW + TW / 2] != 0xFFFF0000u) {
      x2_log_info("d3d8 draw selftest: FAILED -- model-space D3DCULL_CW "
                  "removed the front face (centre 0x%08x).\n",
                  img[(TH / 2) * TW + TW / 2]);
      fails++;
    }
    gpu_offscreen_end();
  }
  gpu_device_destroy();
  x2_log_info(
      "d3d8 draw selftest: %s\n",
      fails ? "FAILED"
            : "PASSED -- a vertex buffer locked and filled through the COM "
              "vtables was rasterised");
  return fails;
}

/*
 * The DEPTH TEST, by pixels.
 *
 * The order of the two draws is the whole test: a NEAR red quad first, then a
 * FAR blue one over the same pixels. With a working depth buffer the red one
 * survives; with none -- which is what this backend had, for 476,531 draws in
 * a menu run -- the later draw wins and the centre comes back blue. So a
 * regression that loses the depth attachment, or builds the pipeline without
 * depth state, turns this from PASSED to a specific accusation rather than to
 * a picture nobody looks at.
 *
 * Pretransformed vertices (XYZRHW) are used so the depth values are written
 * directly rather than through a projection this test would also be testing.
 */
static int depth_selftest(void) {
  struct Vtx {
    float x, y, z, rhw;
    uint32_t color;
  };
  static struct Vtx quad[12];
  static uint32_t img[TW * TH];
  static const struct {
    float z;
    uint32_t color;
  } PASS[2] = {
      {0.25f, 0xFFFF0000u}, /* near, drawn FIRST  */
      {0.75f, 0xFF0000FFu}, /* far,  drawn SECOND */
  };
  D3D8Object *vb;
  D3D8State st;
  D3D8DrawRequest req;
  GpuDraw gd;
  uint32_t args[3], locked, centre;
  int fails = 0, i;

  x2_log_info("\n=== d3d8 depth selftest: the far draw must NOT win ===\n");
  if (!gpu_device_create()) {
    x2_log_info(
        "d3d8 depth selftest: FAILED -- no GPU device, so NOTHING about "
        "the depth test was checked.\n");
    return 1;
  }
  d3d8_resource_install();
  vb = d3d8_vertexbuffer_new(sizeof quad, 0, 0x0044u, 0);
  if (!vb) {
    x2_log_info("d3d8 depth selftest: FAILED -- no vertex buffer.\n");
    gpu_device_destroy();
    return 1;
  }
  d3d8_resource_attach_destructor(vb);

  /* Two full-target quads, six vertices each, wound the way the engine
     winds a front face (the same convention the draw self-test pins down). */
  for (i = 0; i < 2; i++) {
    static const float X[6] = {0.0f, (float)TW, 0.0f,
                               0.0f, (float)TW, (float)TW};
    static const float Y[6] = {0.0f,      0.0f, (float)TH,
                               (float)TH, 0.0f, (float)TH};
    int k;
    for (k = 0; k < 6; k++) {
      struct Vtx *v = &quad[i * 6 + k];
      v->x = X[k];
      v->y = Y[k];
      v->z = PASS[i].z;
      v->rhw = 1.0f;
      v->color = PASS[i].color;
    }
  }

  args[0] = 0;
  args[1] = 0;
  args[2] = guest_malloc(4);
  call_method(vb, 11, args, 4);
  locked = RD32(args[2]);
  if (!locked) {
    x2_log_info("d3d8 depth selftest: FAILED -- Lock returned no pointer.\n");
    gpu_device_destroy();
    return 1;
  }
  memcpy(guest_memory_pointer(locked), quad, sizeof quad);
  call_method(vb, 12, NULL, 0);

  d3d8_state_reset(&st);
  st.vertex_shader = 0x0044u;
  st.stream[0].guest_ptr = d3d8_object_guest(vb);
  st.stream[0].stride = sizeof quad[0];
  /* D3DRS_ZENABLE(7)=TRUE, D3DRS_ZWRITEENABLE(14)=TRUE,
     D3DRS_ZFUNC(23)=D3DCMP_LESSEQUAL(4), D3DRS_CULLMODE(22)=NONE(1) --
     exactly what a title sets before drawing world geometry. */
  d3d8_state_set_render(&st, 7, 1);
  d3d8_state_set_render(&st, 14, 1);
  d3d8_state_set_render(&st, 23, 4);
  d3d8_state_set_render(&st, 22, 1);

  if (!gpu_offscreen_begin(TW, TH, 0.0f, 1.0f, 0.0f, 1.0f)) {
    x2_log_info("d3d8 depth selftest: FAILED -- no off-screen target.\n");
    gpu_device_destroy();
    return 1;
  }
  for (i = 0; i < 2; i++) {
    memset(&req, 0, sizeof req);
    req.vertex_buffer = d3d8_resource_buffer(vb);
    req.stride = sizeof quad[0];
    req.vertex_bytes = sizeof quad;
    req.primitive_type = 4; /* D3DPT_TRIANGLELIST */
    req.primitive_count = 2;
    req.first_vertex = (uint32_t)(i * 6);
    if (!d3d8_build_draw(&st, &req, &gd) || !gpu_draw(&gd)) {
      x2_log_info("d3d8 depth selftest: FAILED -- pass %d was refused, so "
                  "the comparison never happened.\n",
                  i);
      gpu_offscreen_end();
      gpu_device_destroy();
      return fails + 1;
    }
    if (!gd.depth_test || !gd.depth_write) {
      x2_log_info("d3d8 depth selftest: FAILED -- the state said ZENABLE and "
                  "ZWRITEENABLE and the draw came out with test=%d write=%d.\n",
                  gd.depth_test, gd.depth_write);
      fails++;
    }
  }
  if (!gpu_offscreen_read(img, sizeof img)) {
    x2_log_info("d3d8 depth selftest: FAILED -- nothing could be read back.\n");
    gpu_offscreen_end();
    gpu_device_destroy();
    return fails + 1;
  }
  gpu_offscreen_end();

  centre = img[(TH / 2) * TW + TW / 2];
  if (centre == 0xFF0000FFu) {
    x2_log_info("d3d8 depth selftest: FAILED -- the centre is BLUE, so the far "
                "quad drawn second covered the near one. There is no working "
                "depth test, and every scene will paint back to front by "
                "submission order.\n");
    fails++;
  } else if (centre != 0xFFFF0000u) {
    x2_log_info("d3d8 depth selftest: FAILED -- the centre is 0x%08x, neither "
                "the near red quad (0xffff0000) nor the far blue one.\n",
                centre);
    fails++;
  }

  gpu_device_destroy();
  x2_log_info(
      "d3d8 depth selftest: %s\n",
      fails ? "FAILED"
            : "PASSED -- a far quad drawn AFTER a near one does not cover it");
  return fails;
}

/*
 * The multi-stage COUNTER, proved to fire.
 *
 * It reports 0 of ~300,000 draws on this game, which is a finding -- the title
 * is single-stage and the "multi-texture" gap does not exist for it. A zero
 * from a counter nobody has seen move is not a finding, it is an untested
 * branch, so this drives a draw with stage 1 enabled and requires the number
 * to change.
 */
static int multistage_counter_selftest(void) {
  D3D8State st;
  D3D8DrawRequest req;
  GpuDraw gd;
  unsigned long before, after;
  int most_before, most_after, fails = 0;

  x2_log_info("\n=== d3d8 multi-stage counter selftest: it must be able to "
              "count ===\n");
  d3d8_state_reset(&st);
  st.vertex_shader = 0x0044u;
  st.stream[0].guest_ptr = 0; /* refused before the count? */
  d3d8_drawcall_multistage(&before, &most_before);

  /* A draw that is otherwise ordinary, with D3DTSS_COLOROP on stage 1 set to
     MODULATE. It needs a vertex buffer handle only to get past the earlier
     checks; the count happens before anything is rasterised. */
  memset(&req, 0, sizeof req);
  req.vertex_buffer = 1u; /* any non-zero handle */
  req.stride = 20;
  req.vertex_bytes = 20u * 64u;
  req.primitive_type = 4;
  req.primitive_count = 1;
  d3d8_state_set_stage(&st, 1, 1 /* D3DTSS_COLOROP */, 4 /* MODULATE */);
  (void)d3d8_build_draw(&st, &req, &gd);
  d3d8_drawcall_multistage(&after, &most_after);

  if (after != before + 1 || most_after < 1) {
    x2_log_info("d3d8 multi-stage counter selftest: FAILED -- a draw with "
                "stage 1 enabled moved the count from %lu to %lu (max extra "
                "%d). The zero this reports on the game would mean nothing.\n",
                before, after, most_after);
    fails++;
  }
  x2_log_info(
      "d3d8 multi-stage counter selftest: %s\n",
      fails ? "FAILED"
            : "PASSED -- the counter moves when a draw enables stage 1, so its "
              "zero on the game is a measurement");
  return fails;
}

/*
 * TRIANGLEFAN, by pixels, with the negative that matters.
 *
 * A fan is expanded into a triangle list because Vulkan has no fan primitive,
 * and the failure mode of a wrong expansion is not an empty screen -- it is a
 * SUBSET of the fan drawn, which reads as a modelling bug. So the shape here
 * is a four-triangle fan covering the whole target, and all four corners are
 * checked: an expansion that emitted (v0, v1, v2) and stopped, or that walked
 * the vertices as a strip, covers some corners and not others.
 *
 * The negative is the same fan drawn as a two-triangle list over the same six
 * vertices, which must NOT cover the target -- proof that the corners being
 * red is the expansion's doing rather than a background that was red anyway.
 */
static int fan_selftest(void) {
  struct Vtx {
    float x, y, z, rhw;
    uint32_t color;
  };
  /* v0 centre, then the four corners clockwise and back to the first, which
     is a fan of four triangles covering the target. */
  static struct Vtx fan[6];
  static uint32_t img[TW * TH];
  static const float FX[6] = {TW / 2.0f, 0.0f, (float)TW,
                              (float)TW, 0.0f, 0.0f};
  static const float FY[6] = {TH / 2.0f, 0.0f,      0.0f,
                              (float)TH, (float)TH, 0.0f};
  D3D8Object *vb;
  D3D8State st;
  D3D8DrawRequest req;
  GpuDraw gd;
  uint32_t args[3], locked;
  int fails = 0, i;
  static const int PX[4] = {4, TW - 5, 4, TW - 5};
  static const int PY[4] = {4, 4, TH - 5, TH - 5};

  x2_log_info("\n=== d3d8 TRIANGLEFAN selftest: all four corners, not just one "
              "triangle's worth ===\n");
  if (!gpu_device_create()) {
    x2_log_info("d3d8 fan selftest: FAILED -- no GPU device, so NOTHING about "
                "the fan expansion was checked.\n");
    return 1;
  }
  d3d8_resource_install();
  vb = d3d8_vertexbuffer_new(sizeof fan, 0, 0x0044u, 0);
  if (!vb) {
    x2_log_info("d3d8 fan selftest: FAILED -- no vertex buffer.\n");
    gpu_device_destroy();
    return 1;
  }
  d3d8_resource_attach_destructor(vb);
  for (i = 0; i < 6; i++) {
    fan[i].x = FX[i];
    fan[i].y = FY[i];
    fan[i].z = 0.5f;
    fan[i].rhw = 1.0f;
    fan[i].color = 0xFFFF0000u; /* red */
  }
  args[0] = 0;
  args[1] = 0;
  args[2] = guest_malloc(4);
  call_method(vb, 11, args, 4);
  locked = RD32(args[2]);
  if (!locked) {
    x2_log_info("d3d8 fan selftest: FAILED -- Lock returned no pointer.\n");
    gpu_device_destroy();
    return 1;
  }
  memcpy(guest_memory_pointer(locked), fan, sizeof fan);
  call_method(vb, 12, NULL, 0);

  d3d8_state_reset(&st);
  st.vertex_shader = 0x0044u;
  st.stream[0].guest_ptr = d3d8_object_guest(vb);
  st.stream[0].stride = sizeof fan[0];
  d3d8_state_set_render(&st, 22, 1); /* CULLMODE = NONE */

  for (i = 0; i < 2; i++) { /* 0: fan, 1: the control */
    int k, covered = 0;
    if (!gpu_offscreen_begin(TW, TH, 0.0f, 0.0f, 1.0f, 1.0f)) {
      x2_log_info("d3d8 fan selftest: FAILED -- no off-screen target.\n");
      gpu_device_destroy();
      return fails + 1;
    }
    memset(&req, 0, sizeof req);
    req.vertex_buffer = d3d8_resource_buffer(vb);
    req.stride = sizeof fan[0];
    req.vertex_bytes = sizeof fan;
    req.primitive_type = i ? 4u : 6u; /* TRIANGLELIST : TRIANGLEFAN */
    req.primitive_count = i ? 2u : 4u;
    if (!d3d8_build_draw(&st, &req, &gd) || !gpu_draw(&gd)) {
      x2_log_info("d3d8 fan selftest: FAILED -- the %s draw was refused, so "
                  "nothing was compared.\n",
                  i ? "control" : "fan");
      gpu_offscreen_end();
      gpu_device_destroy();
      return fails + 1;
    }
    if (!i && gd.prim != GPU_PRIM_TRIANGLELIST) {
      x2_log_info("d3d8 fan selftest: FAILED -- the fan did not come out as "
                  "a triangle list (prim %d).\n",
                  (int)gd.prim);
      fails++;
    }
    if (!gpu_offscreen_read(img, sizeof img)) {
      x2_log_info("d3d8 fan selftest: FAILED -- nothing could be read "
                  "back.\n");
      gpu_offscreen_end();
      gpu_device_destroy();
      return fails + 1;
    }
    gpu_offscreen_end();
    for (k = 0; k < 4; k++)
      if (img[PY[k] * TW + PX[k]] == 0xFFFF0000u)
        covered++;
    if (!i && covered != 4) {
      x2_log_info("d3d8 fan selftest: FAILED -- %d of 4 corners are red. A "
                  "fan of four triangles covers the whole target; a partial "
                  "expansion covers a wedge of it, which reads as a "
                  "modelling bug rather than a missing primitive.\n",
                  covered);
      for (k = 0; k < 4; k++)
        x2_log_info("    corner (%d,%d) = 0x%08x\n", PX[k], PY[k],
                    img[PY[k] * TW + PX[k]]);
      fails++;
    }
    if (i && covered == 4) {
      x2_log_info("d3d8 fan selftest: FAILED -- the CONTROL (the same six "
                  "vertices as a triangle list) also covers all four "
                  "corners, so covering them proves nothing about the fan "
                  "expansion.\n");
      fails++;
    }
  }

  gpu_device_destroy();
  x2_log_info(
      "d3d8 fan selftest: %s\n",
      fails ? "FAILED"
            : "PASSED -- a four-triangle fan covers all four corners and the "
              "same vertices as a list do not");
  return fails;
}

/*
 * FIXED-FUNCTION LIGHTING, by pixels, and with its own negative.
 *
 * The same quad is drawn twice with only its NORMAL flipped: facing the light
 * it must come back the material's diffuse (red), facing away it must come
 * back the ambient (black). One case alone proves nothing -- a stage that
 * ignored the normal entirely and always returned the material colour would
 * pass a lit-only test, and one that always returned black would pass an
 * unlit-only test. Both, together, are the check.
 *
 * FVF is XYZ|NORMAL with no diffuse: exactly the vertex the game's world
 * geometry uses (measured -- stride 24 with no colour, in X2_FRAME_DUMP).
 */
static int lighting_selftest(void) {
  struct Vtx {
    float x, y, z, nx, ny, nz;
  };
  static struct Vtx quad[6];
  static uint32_t img[TW * TH];
  static const float NDC[6][2] = {{-1, -1}, {1, -1}, {-1, 1},
                                  {-1, 1},  {1, -1}, {1, 1}};
  D3D8Object *vb;
  D3D8State st;
  D3D8DrawRequest req;
  GpuDraw gd;
  uint32_t args[3], locked, centre[2];
  int fails = 0, pass, i;

  x2_log_info("\n=== d3d8 lighting selftest: N.L, both ways ===\n");
  if (!gpu_device_create()) {
    x2_log_info("d3d8 lighting selftest: FAILED -- no GPU device, so NOTHING "
                "about lighting was checked.\n");
    return 1;
  }
  d3d8_resource_install();
  vb = d3d8_vertexbuffer_new(sizeof quad, 0, 0x0012u /* XYZ|NORMAL */, 0);
  if (!vb) {
    x2_log_info("d3d8 lighting selftest: FAILED -- no vertex buffer.\n");
    gpu_device_destroy();
    return 1;
  }
  d3d8_resource_attach_destructor(vb);

  d3d8_state_reset(&st);
  st.vertex_shader = 0x0012u;
  st.stream[0].guest_ptr = d3d8_object_guest(vb);
  st.stream[0].stride = sizeof quad[0];
  d3d8_state_set_render(&st, 22, 1);  /* CULLMODE = NONE */
  d3d8_state_set_render(&st, 137, 1); /* LIGHTING = TRUE */
  d3d8_state_set_render(&st, 139, 0); /* AMBIENT  = black */
  /* No transform is set, so world, view and projection are identity and the
     vertices below are already in clip space. */

  /* A RED diffuse material, nothing else: so a lit pixel is unmistakably
     the material's colour and not the light's. */
  st.material[0] = 1.0f;
  st.material[1] = 0.0f;
  st.material[2] = 0.0f;
  st.material[3] = 1.0f;
  st.material_set = 1;

  /* One DIRECTIONAL light (type 3) shining along +Z, white. */
  d3d8_light_selftest_configure(&st);

  for (pass = 0; pass < 2; pass++) {
    float nz = pass == 0 ? -1.0f : 1.0f; /* toward, then away */
    for (i = 0; i < 6; i++) {
      quad[i].x = NDC[i][0];
      quad[i].y = NDC[i][1];
      quad[i].z = 0.5f;
      quad[i].nx = 0.0f;
      quad[i].ny = 0.0f;
      quad[i].nz = nz;
    }
    args[0] = 0;
    args[1] = 0;
    args[2] = guest_malloc(4);
    call_method(vb, 11, args, 4);
    locked = RD32(args[2]);
    if (!locked) {
      x2_log_info("d3d8 lighting selftest: FAILED -- Lock returned nothing.\n");
      gpu_device_destroy();
      return fails + 1;
    }
    memcpy(guest_memory_pointer(locked), quad, sizeof quad);
    call_method(vb, 12, NULL, 0);

    memset(&req, 0, sizeof req);
    req.vertex_buffer = d3d8_resource_buffer(vb);
    req.stride = sizeof quad[0];
    req.vertex_bytes = sizeof quad;
    req.primitive_type = 4;
    req.primitive_count = 2;
    if (!d3d8_build_draw(&st, &req, &gd)) {
      x2_log_info("d3d8 lighting selftest: FAILED -- the draw could not be "
                  "built.\n");
      gpu_device_destroy();
      return fails + 1;
    }
    if (pass == 0) {
      if (gd.normal_offset != 12) {
        x2_log_info("d3d8 lighting selftest: FAILED -- XYZ|NORMAL decoded "
                    "the normal at %d, not 12.\n",
                    gd.normal_offset);
        fails++;
      }
      if (!gd.lighting || gd.nlights != 1) {
        x2_log_info("d3d8 lighting selftest: FAILED -- the state said "
                    "LIGHTING with one light enabled and the draw came out "
                    "lighting=%d nlights=%d.\n",
                    gd.lighting, gd.nlights);
        fails++;
      }
    }
    /* Cleared to BLUE, so "nothing was drawn" cannot be mistaken for the
       unlit black the second pass expects. */
    if (!gpu_offscreen_begin(TW, TH, 0.0f, 0.0f, 1.0f, 1.0f) ||
        !gpu_draw(&gd) || !gpu_offscreen_read(img, sizeof img)) {
      x2_log_info("d3d8 lighting selftest: FAILED -- pass %d did not "
                  "rasterise.\n",
                  pass);
      gpu_offscreen_end();
      gpu_device_destroy();
      return fails + 1;
    }
    gpu_offscreen_end();
    centre[pass] = img[(TH / 2) * TW + TW / 2];
  }

  if (centre[0] != 0xFFFF0000u) {
    x2_log_info("d3d8 lighting selftest: FAILED -- a quad facing a white "
                "directional light with a red diffuse material came back "
                "0x%08x, not 0xffff0000.\n",
                centre[0]);
    fails++;
  }
  if (centre[1] != 0xFF000000u) {
    x2_log_info("d3d8 lighting selftest: FAILED -- the SAME quad with its "
                "normal reversed came back 0x%08x, not black. %s\n",
                centre[1],
                centre[1] == 0xFF0000FFu
                    ? "That is the clear colour: nothing was drawn at all."
                    : "N.L is not being applied.");
    fails++;
  }

  gpu_device_destroy();
  x2_log_info(
      "d3d8 lighting selftest: %s\n",
      fails ? "FAILED"
            : "PASSED -- lit toward the light, black away from it, from the "
              "same vertices");
  return fails;
}

/*
 * The pixel-shader handle, driven through the real device vtable.
 *
 * The engine's first call is `SetPixelShader(0)` -- MEASURED, not assumed: the
 * unimplemented-method report prints the pushed arguments, and this is what it
 * printed. Handle 0 is D3D8 for "no pixel shader, use the fixed-function
 * pipeline", which this backend implements, so answering it is faithful.
 *
 * A NON-ZERO handle is the case that must not quietly pass. Nothing here can
 * create one -- CreatePixelShader is unimplemented, so a handle cannot exist --
 * and if a shader ever were bound, the fixed-function draw path would silently
 * render something the engine did not ask for. The test that matters is
 * therefore the refusal, and that the refusal leaves the state alone.
 *
 * Needs no GPU and no game: it is entirely about the handle and the stack.
 */
static int pixel_shader_selftest(void) {
  D3D8Object *dev;
  uint32_t args[1], out, hr;
  int fails = 0;

  x2_log_info(
      "\n=== d3d8 pixel-shader selftest: through the device vtable ===\n");
  d3d8_device_install();
  dev = d3d8_object_new(D3D8_IF_IDirect3DDevice8, NULL);
  if (!dev) {
    x2_log_info("d3d8 ps selftest: FAILED -- no device object.\n");
    return 1;
  }
  out = guest_malloc(4);

  args[0] = 0;
  hr = call_method(dev, 88, args, 1); /* SetPixelShader(0) */
  if (hr != D3D_OK) {
    x2_log_info("d3d8 ps selftest: FAILED -- SetPixelShader(0) returned "
                "0x%08x, not D3D_OK. Handle 0 IS the fixed-function "
                "pipeline.\n",
                hr);
    fails++;
  }

  WR32(out, 0xA5A5A5A5u);
  args[0] = out;
  hr = call_method(dev, 89, args, 1); /* GetPixelShader */
  if (hr != D3D_OK || RD32(out) != 0) {
    x2_log_info("d3d8 ps selftest: FAILED -- GetPixelShader returned 0x%08x "
                "and wrote 0x%08x; expected D3D_OK and 0.\n",
                hr, RD32(out));
    fails++;
  }

  args[0] = 0xDEADBEEFu;
  hr = call_method(dev, 88, args, 1); /* a handle nobody made */
  if (hr != D3DERR_INVALIDCALL) {
    x2_log_info("d3d8 ps selftest: FAILED -- SetPixelShader(0xdeadbeef) "
                "returned 0x%08x. A handle this host never created must be "
                "REFUSED, or the fixed-function path would draw in place of a "
                "shader nobody would see was missing.\n",
                hr);
    fails++;
  }

  WR32(out, 0xA5A5A5A5u);
  args[0] = out;
  call_method(dev, 89, args, 1);
  if (RD32(out) != 0) {
    x2_log_info("d3d8 ps selftest: FAILED -- the refused handle 0x%08x was "
                "recorded anyway.\n",
                RD32(out));
    fails++;
  }

  args[0] = 0;
  hr = call_method(dev, 89, args, 1); /* GetPixelShader(NULL) */
  if (hr != D3DERR_INVALIDCALL) {
    x2_log_info("d3d8 ps selftest: FAILED -- GetPixelShader(NULL) returned "
                "0x%08x, not D3DERR_INVALIDCALL.\n",
                hr);
    fails++;
  }

  x2_log_info(
      "d3d8 ps selftest: %s\n",
      fails ? "FAILED"
            : "PASSED -- handle 0 selects the fixed-function pipeline, and a "
              "handle this host never created is refused");
  return fails;
}

/*
 * The gamma ramp, driven through the real device vtable.
 *
 * The engine calls SetGammaRamp(0, <ramp>) during renderer init -- measured,
 * from the unimplemented-method report. A D3DGAMMARAMP is three arrays of 256
 * 16-bit entries, one per channel.
 *
 * This backend cannot programme a hardware gamma ramp: it presents through a
 * Vulkan swapchain and there is no ramp to set. So what matters is not that
 * the call returns, but that the layer knows and SAYS whether the ramp it was
 * given would have changed anything -- an IDENTITY ramp costs nothing to
 * ignore, a curved one is a visible difference from the original game. The
 * test therefore checks the round trip AND the identity verdict, in both
 * directions: a discriminator only trusted after it has been run against both
 * classes has been run against neither.
 */
static void write_ramp(uint32_t base, int curved) {
  int ch, i;
  for (ch = 0; ch < 3; ch++)
    for (i = 0; i < 256; i++) {
      unsigned v = (unsigned)i * 257u; /* the identity ramp */
      if (curved && i == 128)
        v = 0u;
      WR16(base + (uint32_t)(ch * 256 + i) * 2u, (uint16_t)v);
    }
}

static int gamma_selftest(void) {
  D3D8Object *dev;
  uint32_t args[2], ramp, back;
  int fails = 0, i;

  x2_log_info("\n=== d3d8 gamma selftest: through the device vtable ===\n");
  d3d8_device_install();
  dev = d3d8_object_new(D3D8_IF_IDirect3DDevice8, NULL);
  ramp = guest_malloc(3 * 256 * 2);
  back = guest_malloc(3 * 256 * 2);

  write_ramp(ramp, 0);
  args[0] = 0;
  args[1] = ramp;
  call_method(dev, 18, args, 2); /* SetGammaRamp */
  if (d3d8_device_gamma_curved()) {
    x2_log_info("d3d8 gamma selftest: FAILED -- an IDENTITY ramp was reported "
                "as curved, so the warning fires on every run and means "
                "nothing.\n");
    fails++;
  }

  memset(guest_memory_pointer(back), 0xA5, 3 * 256 * 2);
  args[0] = back;
  call_method(dev, 19, args, 1); /* GetGammaRamp */
  for (i = 0; i < 3 * 256; i++)
    if (RD16(back + (uint32_t)i * 2u) != RD16(ramp + (uint32_t)i * 2u)) {
      x2_log_info("d3d8 gamma selftest: FAILED -- entry %d came back as "
                  "0x%04x, not 0x%04x.\n",
                  i, RD16(back + (uint32_t)i * 2u),
                  RD16(ramp + (uint32_t)i * 2u));
      fails++;
      break;
    }

  write_ramp(ramp, 1); /* one entry bent */
  args[0] = 0;
  args[1] = ramp;
  call_method(dev, 18, args, 2);
  if (!d3d8_device_gamma_curved()) {
    x2_log_info("d3d8 gamma selftest: FAILED -- a ramp with a bent entry was "
                "called identity. Then a game that darkens the screen through "
                "gamma would do it silently and nothing would say so.\n");
    fails++;
  }

  args[0] = 0;
  args[1] = 0;
  call_method(dev, 18, args, 2); /* a NULL ramp */
  x2_log_info(
      "d3d8 gamma selftest: %s\n",
      fails ? "FAILED"
            : "PASSED -- the ramp round-trips, and identity is told apart from "
              "curved in both directions");
  return fails;
}

/*
 * IDirect3DDevice8::GetDirect3D -- hand the engine back the object that made
 * this device, with a reference of its own.
 *
 * It was refused, on the grounds that the AddRef could not be balanced. That
 * was wrong twice over: the object layer refcounts already (Direct3DCreate8
 * AddRefs the singleton on every repeat call, exactly as the real one does),
 * and REFUSING is not free -- the engine writes the returned pointer and uses
 * it, so a failed HRESULT with a NULL out-pointer became a SIGSEGV at (nil)
 * one call later.
 *
 * The two cases are both real and are both checked, because they are answered
 * differently: with no IDirect3D8 in existence there is nothing to hand back
 * and INVALIDCALL with a NULL out-pointer is the truth; with one, D3D_OK and a
 * counted reference.
 */
static int getdirect3d_selftest(void) {
  D3D8Object *dev;
  uint32_t args[1], out, hr, before;
  int fails = 0;

  x2_log_info(
      "\n=== d3d8 GetDirect3D selftest: through the device vtable ===\n");
  d3d8_device_install();
  dev = d3d8_object_new(D3D8_IF_IDirect3DDevice8, NULL);
  out = guest_malloc(4);

  /* No Direct3DCreate8 has run in this process. */
  WR32(out, 0xA5A5A5A5u);
  args[0] = out;
  hr = call_method(dev, 6, args, 1);
  if (d3d8_the_direct3d8() == 0) {
    if (hr != D3DERR_INVALIDCALL || RD32(out) != 0) {
      x2_log_info("d3d8 GetDirect3D selftest: FAILED -- with no IDirect3D8 in "
                  "existence it returned 0x%08x and wrote 0x%08x; expected "
                  "INVALIDCALL and a NULL out-pointer.\n",
                  hr, RD32(out));
      fails++;
    }
  }

  /* And with one. Created directly rather than through the import, so this
     needs no guest and no CPU state. */
  before = d3d8_the_direct3d8_refs();
  d3d8_the_direct3d8_ensure();
  WR32(out, 0xA5A5A5A5u);
  args[0] = out;
  hr = call_method(dev, 6, args, 1);
  if (hr != D3D_OK || RD32(out) != d3d8_the_direct3d8()) {
    x2_log_info(
        "d3d8 GetDirect3D selftest: FAILED -- returned 0x%08x and wrote "
        "0x%08x; expected D3D_OK and the IDirect3D8 at 0x%08x.\n",
        hr, RD32(out), d3d8_the_direct3d8());
    fails++;
  }
  if (d3d8_the_direct3d8_refs() <= before) {
    x2_log_info("d3d8 GetDirect3D selftest: FAILED -- the reference count did "
                "not rise (%u then %u). The engine will Release what it was "
                "given, and an unbalanced count makes teardown fail with "
                "nothing to explain it.\n",
                before, d3d8_the_direct3d8_refs());
    fails++;
  }
  x2_log_info(
      "d3d8 GetDirect3D selftest: %s\n",
      fails ? "FAILED"
            : "PASSED -- the IDirect3D8 is handed back with a reference of its "
              "own, and refused honestly when there is none");
  return fails;
}

/*
 * IDirect3DTexture8::GetSurfaceLevel, driven through the real texture vtable.
 *
 * The thing that must not pass is a surface that LOOKS right and owns its own
 * pixels: every check below would still be satisfiable by one -- it has a size,
 * a pitch, an HRESULT -- except the two that matter. So those are the ones
 * designed first:
 *
 *   the level surface's Lock pointer IS the texture's own staging pointer for
 *   that level (a private buffer would give a different address), and
 *
 *   unlocking the surface UPLOADS that level (a counter, incremented only when
 *   the backend accepted the copy -- "returned D3D_OK" and "the texture
 *   changed" are otherwise indistinguishable, and this backend cannot read a
 *   texture back to check the pixels).
 *
 * The level is deliberately 1, not 0: a surface that describes and uploads
 * level 0 whatever it was asked for is the natural way to get this wrong, and
 * level 0 would hide it. 64x32 with 3 levels makes level 1 32x16, so the
 * dimensions are wrong in both axes if the level is ignored.
 */
static int texture_level_selftest(void) {
  D3D8Object *tex;
  D3D8Object *s0, *s1;
  uint32_t args[2], out, desc, lr, hr, g0, g0b, g1, texptr, before;
  unsigned long uploads;
  int fails = 0;

  x2_log_info(
      "\n=== d3d8 texture-level selftest: through the texture vtable ===\n");
  if (!gpu_device_create()) {
    x2_log_info("d3d8 texlevel selftest: FAILED -- no GPU device, so NOTHING "
                "about GetSurfaceLevel was checked.\n");
    return 1;
  }
  d3d8_resource_install();
  d3d8_surface_install();
  tex = d3d8_texture_new(64, 32, 3, 0, D3DFMT_A8R8G8B8, 0);
  if (!tex) {
    x2_log_info("d3d8 texlevel selftest: FAILED -- no texture.\n");
    gpu_device_destroy();
    return 1;
  }
  d3d8_resource_attach_destructor(tex);
  out = guest_malloc(4);
  desc = guest_malloc(32);
  lr = guest_malloc(8);

  /* GetSurfaceLevel is slot 15; (level, ppSurfaceLevel). */
  before = (uint32_t)d3d8_object_refs(tex);
  args[0] = 0;
  args[1] = out;
  WR32(out, 0xA5A5A5A5u);
  hr = call_method(tex, 15, args, 2);
  g0 = RD32(out);
  if (hr != D3D_OK || !g0 || !d3d8_object_from_guest(g0)) {
    x2_log_info("d3d8 texlevel selftest: FAILED -- GetSurfaceLevel(0) returned "
                "0x%08x and wrote 0x%08x, which is not a COM object of this "
                "host's.\n",
                hr, g0);
    gpu_device_destroy();
    return fails + 1;
  }
  s0 = d3d8_object_from_guest(g0);
  if (d3d8_object_iface(s0) != D3D8_IF_IDirect3DSurface8) {
    x2_log_info("d3d8 texlevel selftest: FAILED -- what came back is a %s, not "
                "an IDirect3DSurface8.\n",
                d3d8_iface_name(d3d8_object_iface(s0)));
    fails++;
  }
  if ((uint32_t)d3d8_object_refs(tex) <= before) {
    x2_log_info(
        "d3d8 texlevel selftest: FAILED -- the texture's reference count "
        "did not rise (%u then %u). The engine Releases the surface it "
        "was given, and an unbalanced count destroys the texture early "
        "or never.\n",
        before, (uint32_t)d3d8_object_refs(tex));
    fails++;
  }

  /* The same level twice is the same surface. */
  args[0] = 0;
  args[1] = out;
  call_method(tex, 15, args, 2);
  g0b = RD32(out);
  if (g0b != g0) {
    x2_log_info(
        "d3d8 texlevel selftest: FAILED -- asking for level 0 twice gave "
        "0x%08x then 0x%08x. D3D8's level surfaces are persistent "
        "children, and code that compares them would see two.\n",
        g0, g0b);
    fails++;
  }
  /* and Releasing it gives the texture's count back. */
  call_method(s0, 2, NULL, 0);

  args[0] = 1;
  args[1] = out;
  call_method(tex, 15, args, 2);
  g1 = RD32(out);
  if (!g1 || g1 == g0) {
    x2_log_info(
        "d3d8 texlevel selftest: FAILED -- level 1 came back as 0x%08x, "
        "the same object as level 0.\n",
        g1);
    gpu_device_destroy();
    return fails + 1;
  }
  s1 = d3d8_object_from_guest(g1);

  /* Out of range, and nowhere to write. */
  WR32(out, 0xA5A5A5A5u);
  args[0] = 3;
  args[1] = out;
  hr = call_method(tex, 15, args, 2);
  if (hr != D3DERR_INVALIDCALL || RD32(out) != 0) {
    x2_log_info(
        "d3d8 texlevel selftest: FAILED -- level 3 of a 3-level texture "
        "returned 0x%08x and wrote 0x%08x; expected INVALIDCALL and a "
        "NULL out-pointer.\n",
        hr, RD32(out));
    fails++;
  }
  args[0] = 0;
  args[1] = 0;
  hr = call_method(tex, 15, args, 2);
  if (hr != D3DERR_INVALIDCALL) {
    x2_log_info("d3d8 texlevel selftest: FAILED -- GetSurfaceLevel with a NULL "
                "out-pointer returned 0x%08x.\n",
                hr);
    fails++;
  }

  /* GetDesc (slot 8) must describe LEVEL 1, not the texture. */
  memset(guest_memory_pointer(desc), 0xA5, 32);
  args[0] = desc;
  call_method(s1, 8, args, 1);
  if (RD32(desc + 24u) != 32u || RD32(desc + 28u) != 16u) {
    x2_log_info("d3d8 texlevel selftest: FAILED -- level 1 of a 64x32 texture "
                "describes itself as %ux%u, not 32x16.\n",
                RD32(desc + 24u), RD32(desc + 28u));
    fails++;
  }
  if (RD32(desc + 16u) != 32u * 16u * 4u) {
    x2_log_info(
        "d3d8 texlevel selftest: FAILED -- level 1 says it is %u bytes, "
        "not %u.\n",
        RD32(desc + 16u), 32u * 16u * 4u);
    fails++;
  }
  if (RD32(desc + 0u) != (uint32_t)D3DFMT_A8R8G8B8) {
    x2_log_info("d3d8 texlevel selftest: FAILED -- level 1 reports format %u, "
                "not the texture's.\n",
                RD32(desc + 0u));
    fails++;
  }

  /* THE CHECK: the surface's bytes are the texture's bytes. LockRect is
     slot 9 on the surface (pLockedRect, pRect) and slot 16 on the texture
     (level, pLockedRect, pRect). */
  WR32(lr, 0);
  WR32(lr + 4u, 0);
  args[0] = lr;
  args[1] = 0;
  call_method(s1, 9, args, 2);
  if (RD32(lr) != 32u * 4u) {
    x2_log_info("d3d8 texlevel selftest: FAILED -- level 1 locked with a pitch "
                "of %u; 32 pixels of BGRA8 is %u.\n",
                RD32(lr), 32u * 4u);
    fails++;
  }
  {
    uint32_t surfptr = RD32(lr + 4u);
    uint32_t targs[3];
    targs[0] = 1;
    targs[1] = lr;
    targs[2] = 0;
    WR32(lr, 0);
    WR32(lr + 4u, 0);
    call_method(tex, 16, targs, 3);
    texptr = RD32(lr + 4u);
    call_method(tex, 17, targs, 1); /* texture UnlockRect(1) */
    if (!surfptr || surfptr != texptr) {
      x2_log_info("d3d8 texlevel selftest: FAILED -- the level 1 surface locks "
                  "at 0x%08x and the texture's own level 1 at 0x%08x. The "
                  "surface has storage of its own, so everything the engine "
                  "writes through it is lost.\n",
                  surfptr, texptr);
      fails++;
    }
    if (surfptr) {
      /* Something recognisable, so the upload has real bytes to move. */
      memset(guest_memory_pointer(surfptr), 0x5A, 32u * 16u * 4u);
    }
  }

  /* And unlocking the SURFACE uploads that level of the texture. */
  uploads = d3d8_texture_uploads(tex);
  call_method(s1, 10, NULL, 0); /* surface UnlockRect */
  if (d3d8_texture_uploads(tex) != uploads + 1u) {
    x2_log_info(
        "d3d8 texlevel selftest: FAILED -- unlocking the level 1 surface "
        "uploaded nothing (%lu uploads before and after). The engine "
        "fills textures through these surfaces, so the texture would "
        "stay empty.\n",
        uploads);
    fails++;
  } else if (d3d8_texture_last_upload_level(tex) != 1u) {
    x2_log_info(
        "d3d8 texlevel selftest: FAILED -- unlocking the level 1 surface "
        "uploaded level %u instead.\n",
        d3d8_texture_last_upload_level(tex));
    fails++;
  }

  (void)s0;
  gpu_device_destroy();
  x2_log_info(
      "d3d8 texlevel selftest: %s\n",
      fails ? "FAILED"
            : "PASSED -- a level surface is a VIEW on the texture's own bytes, "
              "describes its own level, and uploads that level on unlock");
  return fails;
}

/*
 * Cube maps, through the IDirect3DCubeTexture8 vtable.
 *
 * The way to get a cube wrong and not notice is to store ONE face: every call
 * succeeds, every lock hands back a pointer, and the six faces quietly share
 * one image. So the check is that two faces of the same level lock at
 * DIFFERENT addresses, exactly one face-chain apart -- and that a face surface
 * is a view on that face's bytes, the same property the 2D level surface has.
 *
 * Face 4 level 1 is used rather than face 0 level 0 because face-ignoring and
 * level-ignoring are the two natural mistakes, and either would pass at 0,0.
 */
static int cube_selftest(void) {
  D3D8Object *cube, *s;
  uint32_t args[3], out, desc, lr, hr, g, p_f4l1, p_f0l1, p_surf;
  uint32_t face_chain;
  unsigned long uploads;
  int fails = 0;

  x2_log_info("\n=== d3d8 cube selftest: six faces, through the vtable ===\n");
  if (!gpu_device_create()) {
    x2_log_info("d3d8 cube selftest: FAILED -- no GPU device, so NOTHING about "
                "cube textures was checked.\n");
    return 1;
  }
  d3d8_resource_install();
  d3d8_surface_install();
  /* 64x64, 3 levels, BGRA8: one face's chain is 64*64*4 + 32*32*4 + 16*16*4. */
  cube = d3d8_cubetexture_new(64, 3, 0, D3DFMT_A8R8G8B8, 0);
  if (!cube) {
    x2_log_info(
        "d3d8 cube selftest: FAILED -- CreateCubeTexture made nothing.\n");
    gpu_device_destroy();
    return 1;
  }
  d3d8_resource_attach_destructor(cube);
  face_chain = 64u * 64u * 4u + 32u * 32u * 4u + 16u * 16u * 4u;
  out = guest_malloc(4);
  desc = guest_malloc(32);
  lr = guest_malloc(8);

  if (d3d8_object_iface(cube) != D3D8_IF_IDirect3DCubeTexture8) {
    x2_log_info("d3d8 cube selftest: FAILED -- what came back is a %s.\n",
                d3d8_iface_name(d3d8_object_iface(cube)));
    fails++;
  }

  /* GetType (slot 10) must say CUBETEXTURE (5), not TEXTURE (3): the engine
     switches on it to decide how to bind. */
  hr = call_method(cube, 10, NULL, 0);
  if (hr != 5u) {
    x2_log_info("d3d8 cube selftest: FAILED -- GetType returned %u, not 5 "
                "(D3DRTYPE_CUBETEXTURE).\n",
                hr);
    fails++;
  }

  /* GetLevelDesc (slot 14) for level 1 of a 64-cube: 32x32. */
  memset(guest_memory_pointer(desc), 0xA5, 32);
  args[0] = 1;
  args[1] = desc;
  call_method(cube, 14, args, 2);
  if (RD32(desc + 24u) != 32u || RD32(desc + 28u) != 32u) {
    x2_log_info("d3d8 cube selftest: FAILED -- level 1 of a 64-cube describes "
                "itself as %ux%u, not 32x32.\n",
                RD32(desc + 24u), RD32(desc + 28u));
    fails++;
  }

  /* LockRect is slot 16: (face, level, pLockedRect, pRect, flags). */
  args[0] = 4;
  args[1] = 1;
  {
    uint32_t a5[5];
    a5[0] = 4;
    a5[1] = 1;
    a5[2] = lr;
    a5[3] = 0;
    a5[4] = 0;
    WR32(lr, 0);
    WR32(lr + 4u, 0);
    hr = call_method(cube, 16, a5, 5);
    p_f4l1 = RD32(lr + 4u);
    if (hr != D3D_OK || !p_f4l1) {
      x2_log_info("d3d8 cube selftest: FAILED -- LockRect(face 4, level 1) "
                  "returned 0x%08x and pointer 0x%08x.\n",
                  hr, p_f4l1);
      fails++;
    }
    if (RD32(lr) != 32u * 4u) {
      x2_log_info("d3d8 cube selftest: FAILED -- face 4 level 1 locked with a "
                  "pitch of %u; 32 pixels of BGRA8 is %u.\n",
                  RD32(lr), 32u * 4u);
      fails++;
    }
    {
      uint32_t u[2];
      u[0] = 4;
      u[1] = 1;
      call_method(cube, 17, u, 2);
    }

    a5[0] = 0;
    WR32(lr, 0);
    WR32(lr + 4u, 0);
    call_method(cube, 16, a5, 5);
    p_f0l1 = RD32(lr + 4u);
    {
      uint32_t u[2];
      u[0] = 0;
      u[1] = 1;
      call_method(cube, 17, u, 2);
    }
  }
  if (p_f0l1 && p_f4l1 && p_f4l1 - p_f0l1 != 4u * face_chain) {
    x2_log_info("d3d8 cube selftest: FAILED -- face 0 level 1 is at 0x%08x and "
                "face 4 level 1 at 0x%08x, %d bytes apart; four face chains is "
                "%u. The faces overlap or are mis-spaced, so writing one face "
                "corrupts another.\n",
                p_f0l1, p_f4l1, (int)(p_f4l1 - p_f0l1), 4u * face_chain);
    fails++;
  }

  /* A face out of range must be refused, not wrapped. */
  {
    uint32_t a5[5];
    a5[0] = 6;
    a5[1] = 0;
    a5[2] = lr;
    a5[3] = 0;
    a5[4] = 0;
    hr = call_method(cube, 16, a5, 5);
    if (hr != D3DERR_INVALIDCALL) {
      x2_log_info("d3d8 cube selftest: FAILED -- LockRect on face 6 of a cube "
                  "returned 0x%08x, not INVALIDCALL.\n",
                  hr);
      fails++;
    }
  }

  /* GetCubeMapSurface (slot 15): (face, level, ppSurface) -- a view on that
     face's bytes, and unlocking it uploads that face. */
  args[0] = 4;
  args[1] = 1;
  args[2] = out;
  WR32(out, 0);
  hr = call_method(cube, 15, args, 3);
  g = RD32(out);
  if (hr != D3D_OK || !g || !d3d8_object_from_guest(g)) {
    x2_log_info(
        "d3d8 cube selftest: FAILED -- GetCubeMapSurface(4, 1) returned "
        "0x%08x and wrote 0x%08x.\n",
        hr, g);
    gpu_device_destroy();
    return fails + 1;
  }
  s = d3d8_object_from_guest(g);
  WR32(lr, 0);
  WR32(lr + 4u, 0);
  {
    uint32_t a2[2];
    a2[0] = lr;
    a2[1] = 0;
    call_method(s, 9, a2, 2);
  }
  p_surf = RD32(lr + 4u);
  if (p_surf != p_f4l1) {
    x2_log_info(
        "d3d8 cube selftest: FAILED -- the face 4 level 1 surface locks "
        "at 0x%08x and the cube's own face 4 level 1 at 0x%08x. The "
        "surface has storage of its own, so everything the engine "
        "writes through it is lost.\n",
        p_surf, p_f4l1);
    fails++;
  }
  /*
   * A SUB-RECTANGLE lock: the movie player locks one every frame, and while
   * this host refused them every movie frame was dropped. (8,4)-(16,12) of
   * a 32x32 BGRA8 level must come back at base + 4*pitch + 8*4, with the
   * pitch unchanged -- a zero offset, the old behaviour dressed up, would
   * put the caller's pixels in the top-left corner.
   */
  {
    uint32_t rc = guest_malloc(16), a2[2], want;
    WR32(rc, 8);
    WR32(rc + 4u, 4);
    WR32(rc + 8u, 16);
    WR32(rc + 12u, 12);
    WR32(lr, 0);
    WR32(lr + 4u, 0);
    a2[0] = lr;
    a2[1] = rc;
    hr = call_method(s, 9, a2, 2);
    want = p_f4l1 + 4u * 32u * 4u + 8u * 4u;
    if (hr != D3D_OK || RD32(lr + 4u) != want) {
      x2_log_info("d3d8 cube selftest: FAILED -- locking (8,4)-(16,12) of a "
                  "32x32 BGRA8 face returned 0x%08x at 0x%08x; the first "
                  "pixel inside that rectangle is at 0x%08x.\n",
                  hr, RD32(lr + 4u), want);
      fails++;
    }
    if (RD32(lr) != 32u * 4u) {
      x2_log_info("d3d8 cube selftest: FAILED -- a sub-rectangle lock changed "
                  "the pitch to %u; the rows are still the surface's rows, so "
                  "it is %u.\n",
                  RD32(lr), 32u * 4u);
      fails++;
    }
    /* Outside the surface must be refused, not clamped. */
    WR32(rc, 0);
    WR32(rc + 4u, 0);
    WR32(rc + 8u, 64);
    WR32(rc + 12u, 8);
    hr = call_method(s, 9, a2, 2);
    if (hr != D3DERR_INVALIDCALL) {
      x2_log_info("d3d8 cube selftest: FAILED -- locking (0,0)-(64,8) of a "
                  "32x32 face returned 0x%08x, not INVALIDCALL.\n",
                  hr);
      fails++;
    }
  }

  uploads = d3d8_texture_uploads(cube);
  call_method(s, 10, NULL, 0); /* surface UnlockRect */
  if (d3d8_texture_uploads(cube) != uploads + 1u) {
    x2_log_info("d3d8 cube selftest: FAILED -- unlocking the face 4 level 1 "
                "surface uploaded nothing (%lu uploads before and after).\n",
                uploads);
    fails++;
  } else if (d3d8_texture_last_upload_level(cube) != 1u) {
    x2_log_info("d3d8 cube selftest: FAILED -- unlocking the face 4 level 1 "
                "surface uploaded level %u instead.\n",
                d3d8_texture_last_upload_level(cube));
    fails++;
  }

  gpu_device_destroy();
  x2_log_info(
      "d3d8 cube selftest: %s\n",
      fails ? "FAILED"
            : "PASSED -- six faces with separate storage, each addressable "
              "through LockRect and through its own surface, each uploading to "
              "its own layer");
  return fails;
}

int d3d8_host_selftest(void) {
  int fails = 0;
  extern int kernel32_thread_alias_selftest(void);
  extern int dsound_selftest(void);

  fails += kernel32_thread_alias_selftest();
  fails += dsound_selftest();
  fails += d3d8_com_selftest();
  fails += caps_selftest();
  fails += pixel_shader_selftest();
  fails += d3d8_vs_selftest();
  fails += d3d8_constants_probe_selftest();
  fails += d3d8_light_selftest(call_method);
  fails += gamma_selftest();
  fails += getdirect3d_selftest();
  fails += texture_level_selftest();
  fails += cube_selftest();
  fails += d3d8_draw_selftest();
  fails += depth_selftest();
  fails += fan_selftest();
  fails += multistage_counter_selftest();
  fails += lighting_selftest();
  x2_log_info("d3d8: SELF-TEST %s -- %d failure(s)\n",
              fails ? "FAILED" : "PASSED", fails);
  return fails;
}
