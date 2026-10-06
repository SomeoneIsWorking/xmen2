/*
 * The VS 1.1 executor: the guest's own shader program, run on the host CPU.
 *
 * It runs what d3d8_vs_decode.cpp decoded; the handle store next door owns
 * object lifetime only. The selftest runs a relative-addressed program on one
 * vertex and across batches with the expected output written out, a lane
 * indexing past the constant file, and an unsupported opcode -- all through
 * the shipping executor, not a test-only copy.
 */
#include "d3d8_vertex_shader_internal.h"

#include "../native/x2_log.h"
#include "d3d8_state.h"

#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <lucent/log_c.h>

static unsigned long g_executions, g_vertices;

void d3d8_vs_execution_counts(unsigned long *draws, unsigned long *vertices) {
  *draws = g_executions;
  *vertices = g_vertices;
}

typedef struct {
  float x[4];
} Vec;

static Vec load_input(const uint8_t *p, unsigned type) {
  Vec v = {{0, 0, 0, 1}};
  unsigned i;
  if (type <= 3) {
    unsigned n = type + 1;
    memcpy(v.x, p, n * sizeof(float));
  } else if (type == 4 || type == 5) {
    /* D3DCOLOR is ARGB numerically / BGRA in little-endian memory and is
       presented to a shader as RGBA. UBYTE4 preserves byte order. */
    if (type == 4) {
      v.x[0] = p[2] / 255.0f;
      v.x[1] = p[1] / 255.0f;
      v.x[2] = p[0] / 255.0f;
      v.x[3] = p[3] / 255.0f;
    } else
      for (i = 0; i < 4; ++i)
        v.x[i] = (float)p[i];
  } else {
    const int16_t *q = (const int16_t *)p;
    unsigned n = type == 6 ? 2 : 4;
    for (i = 0; i < n; ++i)
      v.x[i] = (float)q[i];
  }
  return v;
}

/*
 * Execution runs one instruction over a batch of vertices at a time, with the
 * register file stored component-major: each register component is a row of
 * VS_BATCH lanes, one per vertex. A swizzle then chooses a row rather than
 * shuffling a vector, and each operation is a plain loop over lanes. Run a
 * vertex at a time, re-reading every instruction's operands and swizzles per
 * vertex, this executor was 4.9% of the Dead Zone route's samples.
 *
 * The arithmetic is the per-vertex executor's, expression for expression, so
 * a lane computes exactly the value that vertex did on its own.
 */
enum { VS_BATCH = 64 };
/* Past this magnitude an a0 value cannot name a constant from any base. */
constexpr float kAddressLimit = 65536.0f;
constexpr int kAddressRefused = 1 << 20;
/* The selftest's batched draw: more than one batch, and not a multiple. */
constexpr unsigned kBatchVertices = VS_BATCH + 6u;

typedef float Row[VS_BATCH];

/* The writable and input registers of one batch; the constants are read from
   the caller's array, since every lane sees the same ones. */
typedef struct {
  Row reg[VS_FILE_CONST][4];
  /* floor(a0.x + 0.5) per lane, the constant offset a relative source adds;
     computed on the first relative read after a0 is written. */
  int a0_index[VS_BATCH];
  int a0_current;
} Batch;

/* One source operand resolved for the batch: a row per component. */
typedef struct {
  const float *row[4];
} Operand;

/* Scratch rows an operand is built in when it cannot point into the batch:
   a negated register, a constant, or a relative constant. */
typedef struct {
  Row rows[4];
} OperandRows;

/*
 * floor(a0.x + 0.5) for every lane, without a floorf call per read. Exact
 * while |a0.x + 0.5| < 2^16, where every integer is a float; outside that,
 * and for NaN, no base register can bring the index back into the file, so
 * the lane gets an index every relative read refuses -- as floorf's did.
 */
static void round_addresses(Batch *b, unsigned n) {
  for (unsigned lane = 0; lane < n; ++lane) {
    const float y = b->reg[VS_FILE_ADDR][0][lane] + 0.5f;
    if (!(y > -kAddressLimit && y < kAddressLimit)) {
      b->a0_index[lane] =
          y <= -kAddressLimit ? -kAddressRefused : kAddressRefused;
      continue;
    }
    int t = (int)y;
    if ((float)t > y)
      t -= 1;
    b->a0_index[lane] = t;
  }
  b->a0_current = 1;
}

static int resolve_operand(Batch *b, const float constants[VS_CONSTANTS][4],
                           const D3D8VSSource *src, unsigned n,
                           OperandRows *scratch, Operand *out) {
  unsigned c, lane;
  if (src->relative) {
    int idx[VS_BATCH];
    if (!b->a0_current)
      round_addresses(b, n);
    for (lane = 0; lane < n; ++lane) {
      idx[lane] = (int)src->reg + b->a0_index[lane];
      if (idx[lane] < 0 || idx[lane] >= VS_CONSTANTS) {
        lucent_log_warn(
            "d3d8",
            "VS 1.1 indexed constant c[%d] is outside the %d-register "
            "file; draw refused",
            idx[lane], VS_CONSTANTS);
        return 0;
      }
    }
    for (c = 0; c < 4; ++c) {
      const unsigned component = src->swizzle[c];
      for (lane = 0; lane < n; ++lane)
        scratch->rows[c][lane] = constants[idx[lane]][component];
    }
  } else if (src->reg >= VS_FILE_CONST) {
    for (c = 0; c < 4; ++c) {
      const float value = constants[src->reg - VS_FILE_CONST][src->swizzle[c]];
      for (lane = 0; lane < n; ++lane)
        scratch->rows[c][lane] = value;
    }
  } else if (!src->negate) {
    for (c = 0; c < 4; ++c)
      out->row[c] = b->reg[src->reg][src->swizzle[c]];
    return 1;
  } else {
    for (c = 0; c < 4; ++c)
      for (lane = 0; lane < n; ++lane)
        scratch->rows[c][lane] = b->reg[src->reg][src->swizzle[c]][lane];
  }
  for (c = 0; c < 4; ++c) {
    if (src->negate)
      for (lane = 0; lane < n; ++lane)
        scratch->rows[c][lane] = -scratch->rows[c][lane];
    out->row[c] = scratch->rows[c];
  }
  return 1;
}

/* Row c of the result: rows written after every operand has been read, so a
   destination that is also a source reads its old value. */
static void compute_component(unsigned op, const Operand src[3], unsigned c,
                              unsigned n, float *result) {
  const float *x = src[0].row[c];
  const float *y = src[1].row[c];
  const float *z = src[2].row[c];
  unsigned lane;
  switch (op) {
  case VS_OP_MOV:
    for (lane = 0; lane < n; ++lane)
      result[lane] = x[lane];
    break;
  case VS_OP_ADD:
    for (lane = 0; lane < n; ++lane)
      result[lane] = x[lane] + y[lane];
    break;
  case VS_OP_SUB:
    for (lane = 0; lane < n; ++lane)
      result[lane] = x[lane] - y[lane];
    break;
  case VS_OP_MUL:
    for (lane = 0; lane < n; ++lane)
      result[lane] = x[lane] * y[lane];
    break;
  default: /* VS_OP_MAD: DP3 and DP4 are computed once for every component */
    for (lane = 0; lane < n; ++lane)
      result[lane] = x[lane] * y[lane] + z[lane];
    break;
  }
}

static void compute_dot(const Operand src[3], unsigned components, unsigned n,
                        float *result) {
  unsigned c, lane;
  for (lane = 0; lane < n; ++lane)
    result[lane] = 0;
  for (c = 0; c < components; ++c) {
    const float *x = src[0].row[c];
    const float *y = src[1].row[c];
    for (lane = 0; lane < n; ++lane)
      result[lane] += x[lane] * y[lane];
  }
}

static int execute_batch(const D3D8VSProgram *p,
                         const float constants[VS_CONSTANTS][4], Batch *b,
                         unsigned n) {
  OperandRows scratch[3];
  Row result[4];
  unsigned k, c, s;
  for (k = 0; k < p->count; ++k) {
    const D3D8VSInstruction *insn = &p->insn[k];
    const unsigned nsrc = d3d8_vs_source_count(insn->op);
    Operand src[3];
    for (s = 0; s < nsrc; ++s)
      if (!resolve_operand(b, constants, &insn->src[s], n, &scratch[s],
                           &src[s]))
        return 0;
    for (; s < 3; ++s)
      src[s] = src[0];
    if (insn->op == VS_OP_DP3 || insn->op == VS_OP_DP4) {
      compute_dot(src, insn->op == VS_OP_DP3 ? 3u : 4u, n, result[0]);
      for (c = 0; c < 4; ++c)
        if (insn->mask & (1u << c))
          memcpy(b->reg[insn->dst][c], result[0], n * sizeof(float));
      b->a0_current &= insn->dst != VS_FILE_ADDR;
      continue;
    }
    for (c = 0; c < 4; ++c)
      if (insn->mask & (1u << c))
        compute_component(insn->op, src, c, n, result[c]);
    for (c = 0; c < 4; ++c)
      if (insn->mask & (1u << c))
        memcpy(b->reg[insn->dst][c], result[c], n * sizeof(float));
    b->a0_current &= insn->dst != VS_FILE_ADDR;
  }
  return 1;
}

/* Every register but the constants starts a vertex at zero, and oD0 at one;
   then the declaration's inputs are loaded into their lanes. Only the batch's
   n lanes are set: nothing reads a lane past n, and zeroing the whole file
   was 43 KB per batch however few vertices it held. A declared input is
   loaded whole, so it is not zeroed first. a0_index needs no clearing:
   a0_current = 0 makes the first relative read compute it. */
static void load_batch(const D3D8VSProgram *p, const uint8_t *first_vertex,
                       uint32_t stride, unsigned n, Batch *b) {
  unsigned lane, i, c;
  for (D3D8VSRegisterSet left = p->zeroed; left; left &= left - 1u) {
    const unsigned r = (unsigned)std::countr_zero(left);
    for (c = 0; c < 4; ++c)
      memset(b->reg[r][c], 0, n * sizeof(float));
  }
  b->a0_current = 0;
  for (c = 0; c < 4; ++c)
    for (lane = 0; lane < n; ++lane)
      b->reg[VS_OUT_D0][c][lane] = 1.0f;
  for (i = 0; i < VS_INPUTS; ++i) {
    if (!p->input[i].present)
      continue;
    for (lane = 0; lane < n; ++lane) {
      const Vec v = load_input(
          first_vertex + lane * stride + p->input[i].offset, p->input[i].type);
      for (c = 0; c < 4; ++c)
        b->reg[VS_FILE_INPUT + i][c][lane] = v.x[c];
    }
  }
}

static void store_batch(const Batch *b, unsigned n, D3D8VSOutput *output) {
  unsigned lane, c;
  for (lane = 0; lane < n; ++lane) {
    for (c = 0; c < 4; ++c) {
      output[lane].position[c] = b->reg[VS_OUT_POS][c][lane];
      output[lane].diffuse[c] = b->reg[VS_OUT_D0][c][lane];
    }
    output[lane].texcoord[0] = b->reg[VS_OUT_T0][0][lane];
    output[lane].texcoord[1] = b->reg[VS_OUT_T0][1][lane];
  }
}

int d3d8_vs_execute(uint32_t handle, const float constants[VS_CONSTANTS][4],
                    const void *vertices, uint32_t vertex_bytes,
                    uint32_t stride, uint32_t first, uint32_t count,
                    D3D8VSOutput *output) {
  D3D8VertexShader *s = d3d8_vs_get(handle, "draw");
  const D3D8VSProgram *p;
  Batch b;
  unsigned v, i;
  const auto *base = static_cast<const uint8_t *>(vertices);
  if (!s || !vertices || !stride || !output || !(p = d3d8_vs_program(s)))
    return 0;
  if (first > UINT32_MAX - count ||
      (uint64_t)(first + count) * stride > vertex_bytes) {
    x2_log_error("d3d8: vertex shader draw asks for vertices %u..%u at "
                 "stride %u from a %u-byte source.\n",
                 first, first + count, stride, vertex_bytes);
    return 0;
  }
  for (i = 0; count && i < VS_INPUTS; ++i)
    if (p->input[i].present && p->input[i].end > stride) {
      x2_log_error("d3d8: declaration input v%u ends at byte %u, "
                   "past stride %u.\n",
                   i, p->input[i].end, stride);
      return 0;
    }
  for (v = 0; v < count; v += VS_BATCH) {
    const unsigned n = count - v < VS_BATCH ? count - v : VS_BATCH;
    load_batch(p, base + (size_t)(first + v) * stride, stride, n, &b);
    if (!execute_batch(p, constants, &b, n))
      return 0;
    store_batch(&b, n, output + v);
  }
  g_executions++;
  g_vertices += count;
  return 1;
}

int d3d8_vs_selftest(void) {
  /* v0=float4 position, v1=float1 matrix selector, v2=float4 colour. */
  static const uint32_t decl[] = {0x20000000, 0x40030000, 0x40000001,
                                  0x40030002, 0xffffffff};
  static const uint32_t code[] = {
      0xfffe0101, 0x00000001, 0xb0010000, 0x90000001, /* mov a0.x, v1.x */
      0x00000009, 0xc0010000, 0xa0e42001, 0x90e40000, 0x00000009,
      0xc0020000, 0xa0e42002, 0x90e40000, 0x00000009, 0xc0040000,
      0xa0e42003, 0x90e40000, 0x00000009, 0xc0080000, 0xa0e42004,
      0x90e40000, 0x00000001, 0x500f0000, 0x90e40002, /* mov oD0, v2 */
      0x0000ffff};
  static const uint32_t bad_code[] = {0xfffe0101, 0x00001234, 0x800f0000,
                                      0x0000ffff};
  static const uint32_t zero_start_code[] = {
      0xfffe0101, 0x00000002, 0xc00f0000,
      0x80e40000, 0x90e40000,             /* add oPos, r0, v0 */
      0x00000001, 0x800f0000, 0x90e40000, /* mov r0, v0 */
      0x0000ffff};
  struct {
    float p[4], selector, colour[4];
  } vertex = {{1, 2, 3, 1}, 1, {0.25f, 0.5f, 0.75f, 1.0f}};
  float c[VS_CONSTANTS][4] = {{0}};
  D3D8VSOutput out;
  decltype(vertex) batch[kBatchVertices];
  D3D8VSOutput batch_out[kBatchVertices];
  uint32_t h, bad;
  int fails = 0;

  /* a0.x=1, so c[a0+1..4] = c2..c5. Four independent rows make
     relative addressing, masks, and DP4 all observable. */
  c[2][0] = 2;
  c[3][1] = 3;
  c[4][2] = 4;
  c[5][3] = 5;
  h = d3d8_vs_create(decl, code, 0);
  if (!h ||
      !d3d8_vs_execute(h, c, &vertex, sizeof vertex, sizeof vertex, 0, 1,
                       &out) ||
      fabsf(out.position[0] - 2) > 0.0001f ||
      fabsf(out.position[1] - 6) > 0.0001f ||
      fabsf(out.position[2] - 12) > 0.0001f ||
      fabsf(out.position[3] - 5) > 0.0001f ||
      memcmp(out.diffuse, vertex.colour, sizeof vertex.colour) != 0) {
    x2_log_info("d3d8 VS selftest: FAILED -- relative DP4 output was "
                "[%g %g %g %g], expected [2 6 12 5].\n",
                out.position[0], out.position[1], out.position[2],
                out.position[3]);
    fails++;
  }
  if (h)
    d3d8_vs_delete(h);

  /* Across batches: 70 vertices, each selecting its own rows through a0.
     With c[j] = (j, 2j, 3j, 4j) and v0 = (v, 1, 0, 0), a vertex with
     selector s reads c[s+1..s+4] and gets oPos = (s+1, s+2, s+3, s+4) *
     (v + 2): exact, and
     different in every lane. */
  h = d3d8_vs_create(decl, code, 0);
  for (unsigned j = 0; j < 12; ++j)
    for (unsigned k = 0; k < 4; ++k)
      c[j][k] = (float)(j * (k + 1));
  for (unsigned v = 0; v < kBatchVertices; ++v) {
    batch[v] = vertex;
    batch[v].p[0] = (float)v;
    batch[v].p[1] = 1;
    batch[v].p[2] = 0;
    batch[v].p[3] = 0;
    batch[v].selector = (float)(v % 4u);
  }
  if (!h || !d3d8_vs_execute(h, c, batch, sizeof batch, sizeof batch[0], 0,
                             kBatchVertices, batch_out)) {
    x2_log_info("d3d8 VS selftest: FAILED -- a %u-vertex draw was refused.\n",
                kBatchVertices);
    fails++;
  } else {
    for (unsigned v = 0; v < kBatchVertices; ++v) {
      const float scale = (float)(v + 2u);
      const float s = (float)(v % 4u);
      if (batch_out[v].position[0] != (s + 1) * scale ||
          batch_out[v].position[1] != (s + 2) * scale ||
          batch_out[v].position[2] != (s + 3) * scale ||
          batch_out[v].position[3] != (s + 4) * scale) {
        x2_log_info("d3d8 VS selftest: FAILED -- vertex %u of a batched "
                    "draw has [%g %g %g %g].\n",
                    v, batch_out[v].position[0], batch_out[v].position[1],
                    batch_out[v].position[2], batch_out[v].position[3]);
        fails++;
        break;
      }
    }
  }
  /* One lane past the constant file, in the second batch, refuses the
     draw. */
  batch[kBatchVertices - 1u].selector = (float)VS_CONSTANTS;
  if (h && d3d8_vs_execute(h, c, batch, sizeof batch, sizeof batch[0], 0,
                           kBatchVertices, batch_out)) {
    x2_log_info("d3d8 VS selftest: FAILED -- a relative index past the "
                "constant file in lane %u was not refused.\n",
                kBatchVertices - 1u);
    fails++;
  }
  if (h)
    d3d8_vs_delete(h);

  /* A register starts every vertex at zero, in every batch: r0 is read
     before this program writes it, and oT0 is never written. Writing r0
     afterwards makes a second batch that kept the first one's lanes read
     them back. */
  h = d3d8_vs_create(decl, zero_start_code, 0);
  if (h) {
    /* r0, and the stored oPos and oT0 -- not v0, which the declaration
       fills, nor r1, which the program never names. */
    const D3D8VSRegisterSet want = (D3D8VSRegisterSet{1} << VS_FILE_TEMP) |
                                   (D3D8VSRegisterSet{1} << VS_OUT_POS) |
                                   (D3D8VSRegisterSet{1} << VS_OUT_T0);
    const D3D8VSProgram *zp = d3d8_vs_program(d3d8_vs_get(h, "selftest"));
    if (!zp || zp->zeroed != want) {
      x2_log_info("d3d8 VS selftest: FAILED -- the zero-start program "
                  "zeroes register set 0x%llx, expected 0x%llx.\n",
                  zp ? (unsigned long long)zp->zeroed : 0ull,
                  (unsigned long long)want);
      fails++;
    }
  }
  for (unsigned v = 0; v < kBatchVertices; ++v) {
    batch[v].p[0] = (float)(v + 1u);
    batch[v].p[1] = -(float)(v + 3u);
    batch[v].p[2] = 0.5f;
    batch[v].p[3] = 1;
    batch[v].selector = 0;
  }
  if (!h || !d3d8_vs_execute(h, c, batch, sizeof batch, sizeof batch[0], 0,
                             kBatchVertices, batch_out)) {
    x2_log_info("d3d8 VS selftest: FAILED -- the zero-start draw was "
                "refused.\n");
    fails++;
  } else {
    for (unsigned v = 0; v < kBatchVertices; ++v) {
      if (memcmp(batch_out[v].position, batch[v].p, sizeof batch[v].p) != 0 ||
          batch_out[v].texcoord[0] != 0.0f ||
          batch_out[v].texcoord[1] != 0.0f) {
        x2_log_info("d3d8 VS selftest: FAILED -- vertex %u read a register "
                    "it never wrote as [%g %g %g %g], texcoord [%g %g].\n",
                    v, batch_out[v].position[0], batch_out[v].position[1],
                    batch_out[v].position[2], batch_out[v].position[3],
                    batch_out[v].texcoord[0], batch_out[v].texcoord[1]);
        fails++;
        break;
      }
    }
  }
  if (h)
    d3d8_vs_delete(h);

  bad = d3d8_vs_create(decl, bad_code, 0);
  if (!bad || d3d8_vs_execute(bad, c, &vertex, sizeof vertex, sizeof vertex, 0,
                              1, &out)) {
    x2_log_info("d3d8 VS selftest: FAILED -- unsupported opcode 0x1234 was not "
                "refused by the shipping executor.\n");
    fails++;
  }
  if (bad)
    d3d8_vs_delete(bad);
  x2_log_info("d3d8 VS selftest: %s -- relative-addressed program on one "
              "vertex and across batches, zero-started registers, an "
              "out-of-file lane, and an unsupported opcode all exercised\n",
              fails ? "FAILED" : "PASSED");
  return fails;
}
