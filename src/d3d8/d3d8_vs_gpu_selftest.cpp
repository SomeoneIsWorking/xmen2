/*
 * The GPU's VS 1.1 interpreter against the CPU executor, by pixels.
 *
 * Issue #187 moved the guest's vertex programs onto the GPU and kept the CPU
 * executor as the reference. This draws one program through the production
 * draw builder twice -- once as the game's draws now run, with the program
 * and the guest's vertex buffer on the GPU, once through the executor into a
 * buffer of its outputs -- and requires the two images to be identical.
 *
 * The program touches every part of the GPU form: a0 taken from a UBYTE4
 * input and used as a relative constant index (two triangles select two
 * matrices), DP4, DP3, MUL, MAD with a negated replicate-swizzled source, ADD
 * and SUB with swizzles and partial write masks, a D3DCOLOR input, and all
 * three outputs. oT0 reaches the pixels through a point-sampled texture of
 * sixteen different texels. Every constant and input is a dyadic fraction or
 * a colour byte of 0 or 255, so both executors compute the same bits and any
 * difference is a semantic one.
 *
 * Its negatives are designed first: identical images prove nothing unless
 * each draw provably took its own path and the image is not trivially
 * uniform, so both are checked before the comparison.
 */
#include "d3d8_vs_gpu_selftest.h"
#include "../native/x2_log.h"

#include "d3d8_drawcall.h"
#include "d3d8_resource.h"
#include "d3d8_selftest_call.h"
#include "d3d8_state.h"
#include "d3d8_vertex_shader.h"
#include "d3d8_vs_draw.h"

#include "gpu_device.h"
#include "gpu_draw.h"
#include "guest_heap.h"
#include "guest_memory.h"
#include "x86rt.h"

#include <stdint.h>
#include <string.h>

enum { TARGET = 64, VERTICES = 6, TEXELS = 4 };

/* Register types and source modifiers of the VS 1.1 token stream. */
enum { R_TEMP = 0, R_INPUT = 1, R_CONST = 2, R_ADDR = 3, R_POS = 4, R_D0 = 5 };
enum { R_T0 = 6 };
enum { RELATIVE = 0x2000u, NEGATE = 0x01000000u };
enum { XYZW = 0xe4u, XXXX = 0x00u, YXZW = 0xe1u, ZWZW = 0xeeu };
enum { MOV = 1, ADD = 2, SUB = 3, MAD = 4, MUL = 5, DP3 = 8, DP4 = 9 };

#define DST(type, reg, mask)                                                   \
  (0x80000000u | ((uint32_t)(type) << 28) | ((uint32_t)(mask) << 16) | (reg))
#define SRC(type, reg, swizzle)                                                \
  (0x80000000u | ((uint32_t)(type) << 28) | ((uint32_t)(swizzle) << 16) | (reg))

/* v0 float3 position, v1 D3DCOLOR, v2 float2 uv, v3 UBYTE4 selector. */
static const uint32_t kDeclaration[] = {0x20000000u, 0x40020000u, 0x40040001u,
                                        0x40010002u, 0x40050003u, 0xffffffffu};

/* DP3 writes oD0.w before MAD writes oD0.xyz, so the MAD's mask is what keeps
   the alpha: a write mask ignored anywhere shows in the pixels. */
// clang-format off
static const uint32_t kProgram[] = {
    0xfffe0101u,
    MOV, DST(R_ADDR, 0, 1), SRC(R_INPUT, 3, XXXX),
    DP4, DST(R_POS, 0, 1), SRC(R_INPUT, 0, XYZW), SRC(R_CONST, 0, XYZW) | RELATIVE,
    DP4, DST(R_POS, 0, 2), SRC(R_INPUT, 0, XYZW), SRC(R_CONST, 1, XYZW) | RELATIVE,
    DP4, DST(R_POS, 0, 4), SRC(R_INPUT, 0, XYZW), SRC(R_CONST, 2, XYZW) | RELATIVE,
    DP4, DST(R_POS, 0, 8), SRC(R_INPUT, 0, XYZW), SRC(R_CONST, 3, XYZW) | RELATIVE,
    DP3, DST(R_D0, 0, 8), SRC(R_INPUT, 1, XYZW), SRC(R_CONST, 11, XYZW),
    MUL, DST(R_TEMP, 0, 0xf), SRC(R_INPUT, 1, XYZW), SRC(R_CONST, 8, XYZW),
    MAD, DST(R_D0, 0, 7), SRC(R_TEMP, 0, XYZW), SRC(R_CONST, 9, XXXX),
         SRC(R_CONST, 10, XYZW) | NEGATE,
    ADD, DST(R_TEMP, 1, 3), SRC(R_INPUT, 2, YXZW), SRC(R_CONST, 12, XYZW),
    SUB, DST(R_T0, 0, 3), SRC(R_TEMP, 1, XYZW), SRC(R_CONST, 12, ZWZW),
    0x0000ffffu};
// clang-format on

typedef struct {
  float position[3];
  uint32_t colour; /* D3DCOLOR, 0xAARRGGBB */
  float uv[2];
  uint8_t selector[4];
} Vertex;

/* The left triangle takes c0..c3, the identity; the right one c4..c7, which
   halves x and moves it right. */
static const Vertex kVertices[VERTICES] = {
    {{-0.9f, -0.9f, 0.5f}, 0xFFFF0000u, {0.0f, 0.0f}, {0, 0, 0, 0}},
    {{-0.1f, -0.9f, 0.5f}, 0xFF00FF00u, {1.0f, 0.0f}, {0, 0, 0, 0}},
    {{-0.5f, 0.9f, 0.5f}, 0xFF0000FFu, {0.5f, 1.0f}, {0, 0, 0, 0}},
    {{-0.8f, -0.8f, 0.5f}, 0xFFFFFF00u, {0.0f, 1.0f}, {4, 0, 0, 0}},
    {{0.8f, -0.8f, 0.5f}, 0xFF00FFFFu, {1.0f, 1.0f}, {4, 0, 0, 0}},
    {{0.0f, 0.8f, 0.5f}, 0xFFFFFFFFu, {0.5f, 0.0f}, {4, 0, 0, 0}}};

static const float kConstants[13][4] = {{1, 0, 0, 0},
                                        {0, 1, 0, 0},
                                        {0, 0, 1, 0},
                                        {0, 0, 0, 1},
                                        {0.5f, 0, 0, 0.5f},
                                        {0, 1, 0, 0},
                                        {0, 0, 1, 0},
                                        {0, 0, 0, 1},
                                        {1, 0.5f, 1, 1},
                                        {0.5f, 0, 0, 0},
                                        {-0.25f, 0, 0.25f, 0},
                                        {0.25f, 0.25f, 0.5f, 0.25f},
                                        {0.25f, 0.25f, 0.25f, 0}};

/* The vertices in a vertex buffer, locked and filled through its vtable as
   the engine fills one; the guest address of the bytes, or 0. */
static uint32_t fill_vertex_buffer(D3D8Object *vb) {
  uint32_t args[3], locked;
  args[0] = 0;
  args[1] = 0;
  args[2] = guest_malloc(4);
  d3d8_selftest_call(vb, 11, args, 4); /* Lock */
  locked = RD32(args[2]);
  if (locked) {
    memcpy(guest_memory_pointer(locked), kVertices, sizeof kVertices);
    d3d8_selftest_call(vb, 12, NULL, 0); /* Unlock */
  }
  return locked;
}

static GpuTexture make_texture(void) {
  uint32_t texels[TEXELS * TEXELS];
  GpuTexture t = gpu_texture_create(TEXELS, TEXELS, GPU_FMT_BGRA8, 1);
  for (unsigned i = 0; i < TEXELS * TEXELS; i++) {
    texels[i] = 0xFF000000u | (i * 16u + 15u) << 16 | (255u - i * 16u) << 8 |
                ((i * 5u) % 16u) * 16u;
  }
  if (t && !gpu_texture_upload(t, 0, texels, sizeof texels)) {
    gpu_texture_destroy(t);
    t = 0;
  }
  return t;
}

/* The draw, textured, into `image`. */
static int render(GpuDraw *draw, GpuTexture texture, uint32_t *image) {
  int ok;
  draw->texture = texture;
  draw->texop = GPU_TEXOP_MODULATE;
  draw->texture_point = 1;
  if (!gpu_offscreen_begin(TARGET, TARGET, 0.0f, 0.0f, 0.0f, 0.0f)) {
    return 0;
  }
  ok = gpu_draw(draw) &&
       gpu_offscreen_read(image, TARGET * TARGET * sizeof *image);
  gpu_offscreen_end();
  return ok;
}

/* Coverage and variety: identical images must also be informative ones. */
static int image_informative(const uint32_t *image) {
  unsigned covered = 0, distinct = 0;
  uint32_t seen[32];
  for (unsigned i = 0; i < TARGET * TARGET; i++) {
    unsigned j = 0;
    if (!image[i]) {
      continue;
    }
    covered++;
    while (j < distinct && seen[j] != image[i]) {
      j++;
    }
    if (j == distinct && distinct < 32u) {
      seen[distinct++] = image[i];
    }
  }
  if (covered < TARGET * TARGET / 4u || distinct < 32u) {
    x2_log_info("d3d8 VS GPU selftest: FAILED -- the image covers %u of %u "
                "pixels with %u distinct value(s); agreement on it would "
                "prove little.\n",
                covered, TARGET * TARGET, distinct);
    return 0;
  }
  return 1;
}

static int compare(const uint32_t *gpu, const uint32_t *cpu) {
  unsigned differ = 0, first = 0;
  for (unsigned i = 0; i < TARGET * TARGET; i++) {
    if (gpu[i] != cpu[i] && !differ++) {
      first = i;
    }
  }
  if (differ) {
    x2_log_info("d3d8 VS GPU selftest: FAILED -- %u of %u pixels differ; "
                "the first, (%u,%u), is 0x%08x on the GPU and 0x%08x from "
                "the CPU executor.\n",
                differ, TARGET * TARGET, first % TARGET, first / TARGET,
                gpu[first], cpu[first]);
    return 1;
  }
  return 0;
}

/* Both draws, from one device state; 0 when either could not be made. */
static int draw_both(D3D8State *st, D3D8Object *vb, uint32_t guest_bytes,
                     GpuTexture texture, uint32_t *gpu_image,
                     uint32_t *cpu_image) {
  D3D8DrawRequest req;
  GpuDraw gpu, cpu;
  int ok;

  memset(&req, 0, sizeof req);
  req.vertex_buffer = d3d8_resource_buffer(vb);
  req.vertex_guest_bytes = guest_bytes;
  req.stride = sizeof(Vertex);
  req.vertex_bytes = sizeof kVertices;
  req.primitive_type = 4; /* D3DPT_TRIANGLELIST */
  req.primitive_count = VERTICES / 3;
  if (!d3d8_build_draw(st, &req, &gpu)) {
    x2_log_info("d3d8 VS GPU selftest: FAILED -- the draw was not built.\n");
    return 0;
  }
  if (!gpu.vs_program || gpu.owns_vertices) {
    x2_log_info("d3d8 VS GPU selftest: FAILED -- the program did not take the "
                "GPU path; the comparison would be the executor with "
                "itself.\n");
    d3d8_release_draw(&gpu);
    return 0;
  }
  cpu = gpu;
  if (!d3d8_vs_draw_source_on(st, &req, D3D8_VS_ON_CPU, &cpu) ||
      cpu.vs_program || !cpu.owns_vertices) {
    x2_log_info("d3d8 VS GPU selftest: FAILED -- the CPU executor's draw "
                "could not be made.\n");
    d3d8_release_draw(&cpu);
    return 0;
  }
  ok = render(&gpu, texture, gpu_image) && render(&cpu, texture, cpu_image);
  d3d8_release_draw(&cpu);
  if (!ok) {
    x2_log_info("d3d8 VS GPU selftest: FAILED -- a draw was refused or "
                "could not be read back.\n");
  }
  return ok;
}

int d3d8_vs_gpu_selftest(void) {
  uint32_t gpu_image[TARGET * TARGET], cpu_image[TARGET * TARGET];
  D3D8State st;
  D3D8Object *vb;
  GpuTexture texture = 0;
  uint32_t shader = 0, guest_bytes = 0;
  int fails = 1;

  x2_log_info("\n=== d3d8 VS GPU selftest: the GPU's program against the CPU "
              "executor ===\n");
  if (!gpu_device_create()) {
    x2_log_info("d3d8 VS GPU selftest: FAILED -- no GPU device.\n");
    return 1;
  }
  d3d8_resource_install();
  vb = d3d8_vertexbuffer_new(sizeof kVertices, 0, 0, 0);
  if (vb) {
    d3d8_resource_attach_destructor(vb);
    guest_bytes = fill_vertex_buffer(vb);
  }
  shader = d3d8_vs_create(kDeclaration, kProgram, 0);
  texture = make_texture();
  if (!guest_bytes || !shader || !texture) {
    x2_log_info("d3d8 VS GPU selftest: FAILED -- setup: vertex bytes 0x%08x, "
                "shader 0x%08x, texture %u.\n",
                guest_bytes, shader, texture);
  } else {
    d3d8_state_reset(&st);
    st.vertex_shader = shader;
    st.stream[0].guest_ptr = d3d8_object_guest(vb);
    st.stream[0].stride = sizeof(Vertex);
    d3d8_state_set_render(&st, 22, 1); /* D3DCULL_NONE */
    memcpy(st.vertex_shader_constant, kConstants, sizeof kConstants);
    if (draw_both(&st, vb, guest_bytes, texture, gpu_image, cpu_image) &&
        image_informative(gpu_image)) {
      fails = compare(gpu_image, cpu_image);
    }
  }
  if (texture) {
    gpu_texture_destroy(texture);
  }
  if (shader) {
    d3d8_vs_delete(shader);
  }
  gpu_device_destroy();
  x2_log_info("d3d8 VS GPU selftest: %s\n",
              fails ? "FAILED"
                    : "PASSED -- the GPU's program and the CPU executor drew "
                      "the same pixels");
  return fails;
}
