#include "../native/x2_log.h"
/*
 * The VS 1.1 executor: the guest's own shader program, run on the host CPU.
 *
 * Split from the handle store next door, which owns object lifetime only. This
 * is the part that has to be RIGHT about the instruction set -- register
 * encodings, relative addressing through a0, write masks, and the opcodes it
 * does not implement, which are REFUSED rather than treated as no-ops. The
 * selftest runs one positive program (relative-addressed DP4s, with the
 * expected output written out) and one negative (an unsupported opcode that
 * must be turned away by the shipping executor, not by a test-only copy).
 */
#include "d3d8_vertex_shader_internal.h"

#include "d3d8_state.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <lucent/log_c.h>

#define VS_CONSTANTS D3D8_MAX_VS_CONSTANTS

static unsigned long g_executions, g_vertices;

void d3d8_vs_execution_counts(unsigned long *draws, unsigned long *vertices) {
  *draws = g_executions;
  *vertices = g_vertices;
}

typedef struct {
  float x[4];
} Vec;

/*
 * The flat register file a decoded program addresses: temporaries, inputs,
 * a0, outputs (oPos, oFog, oPts, oD0-1, oT0-7) and the constants.
 */
enum {
  FILE_TEMP = 0,
  FILE_INPUT = FILE_TEMP + 12,
  FILE_ADDR = FILE_INPUT + VS_INPUTS,
  FILE_OUT = FILE_ADDR + 1,
  FILE_CONST = FILE_OUT + 13,
  FILE_SIZE = FILE_CONST + VS_CONSTANTS,
};
enum { OUT_POS = FILE_OUT, OUT_D0 = FILE_OUT + 3, OUT_T0 = FILE_OUT + 5 };

enum {
  OP_MOV = 1,
  OP_ADD = 2,
  OP_SUB = 3,
  OP_MAD = 4,
  OP_MUL = 5,
  OP_DP3 = 8,
  OP_DP4 = 9
};

static unsigned reg_type(uint32_t t) {
  return ((t >> 28) & 7u) | ((t >> 8) & 0x18u);
}

/* Bytes per declaration input type: FLOAT1-4, D3DCOLOR, UBYTE4, SHORT2, SHORT4.
 */
constexpr unsigned kInputTypeBytes[8] = {4, 8, 12, 16, 4, 4, 4, 8};

static unsigned data_size(unsigned type) {
  return type < 8 ? kInputTypeBytes[type] : 0;
}

static int decode_inputs(const D3D8VertexShader *s,
                         D3D8VSInput input[VS_INPUTS], uint16_t *input_end) {
  unsigned stream = 0, offset[16] = {0}, i;
  memset(input, 0, VS_INPUTS * sizeof *input);
  *input_end = 0;
  for (i = 0; i < s->declaration_dwords; ++i) {
    uint32_t t = s->declaration[i], kind = (t >> 29) & 7u;
    if (t == 0xffffffffu)
      return 1;
    if (kind == 1) {
      stream = t & 0xfu;
      if (stream != 0) {
        x2_log_error("d3d8: vertex declaration selects stream %u; "
                     "the VS executor currently has only stream 0.\n",
                     stream);
        return 0;
      }
    } else if (kind == 2) {
      if (t & 0x10000000u) {
        offset[stream] += ((t >> 16) & 0xfu) * 4u;
      } else {
        unsigned reg = t & 0x1fu, type = (t >> 16) & 0xfu;
        unsigned n = data_size(type);
        if (reg >= VS_INPUTS || !n) {
          x2_log_error("d3d8: vertex declaration REG %u type %u "
                       "cannot be represented.\n",
                       reg, type);
          return 0;
        }
        input[reg].present = 1;
        input[reg].offset = (uint16_t)offset[stream];
        input[reg].type = (uint8_t)type;
        input[reg].end = (uint16_t)(offset[stream] + n);
        if (input[reg].end > *input_end)
          *input_end = input[reg].end;
        offset[stream] += n;
      }
    } else if (kind != 0) {
      x2_log_error("d3d8: vertex declaration token 0x%08x has "
                   "unsupported token type %u.\n",
                   t, kind);
      return 0;
    }
  }
  x2_log_error("d3d8: scanned %u declaration token(s), but no END was "
               "reachable in the copied stream.\n",
               s->declaration_dwords);
  return 0;
}

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

/* The flat register a writable register token names, or -1. */
static int writable_register(unsigned type, unsigned n) {
  if (type == 0 && n < 12)
    return FILE_TEMP + (int)n;
  if (type == 3 && n == 0)
    return FILE_ADDR;
  if (type == 4 && n < 3)
    return FILE_OUT + (int)n;
  if (type == 5 && n < 2)
    return FILE_OUT + 3 + (int)n;
  if (type == 6 && n < 8)
    return FILE_OUT + 5 + (int)n;
  return -1;
}

static int decode_source(uint32_t t, D3D8VSSource *src) {
  unsigned type = reg_type(t), n = t & 0x7ffu, i;
  int reg = -1;
  memset(src, 0, sizeof *src);
  if (type == 1 && n < VS_INPUTS) {
    reg = FILE_INPUT + (int)n;
  } else if (type == 2) {
    if (t & 0x00002000u) {
      src->relative = 1;
      reg = (int)n; /* the base; the constant is chosen per vertex */
    } else if (n < VS_CONSTANTS) {
      reg = FILE_CONST + (int)n;
    } else {
      lucent_log_warn(
          "d3d8",
          "VS 1.1 indexed constant c[%d] is outside the %d-register "
          "file; draw refused",
          (int)n, VS_CONSTANTS);
      return 0;
    }
  } else if (type != 1) {
    reg = writable_register(type, n);
  }
  if (reg < 0) {
    x2_log_error("d3d8: VS 1.1 source register type %u number %u is "
                 "unsupported.\n",
                 type, n);
    return 0;
  }
  src->reg = (uint16_t)reg;
  for (i = 0; i < 4; ++i)
    src->swizzle[i] = (uint8_t)((t >> (16 + i * 2)) & 3u);
  if (((t >> 24) & 0xfu) == 1) {
    src->negate = 1;
  } else if ((t >> 24) & 0xfu) {
    x2_log_error("d3d8: VS 1.1 source modifier %u is unsupported.\n",
                 (t >> 24) & 0xfu);
    return 0;
  }
  return 1;
}

static unsigned source_count(unsigned op) {
  switch (op) {
  case OP_MOV:
    return 1;
  case OP_ADD:
  case OP_SUB:
  case OP_MUL:
  case OP_DP3:
  case OP_DP4:
    return 2;
  case OP_MAD:
    return 3;
  default:
    return 0;
  }
}

/* Decode the declaration and the program, logging the first reason either
   cannot run. */
static int decode_program(const D3D8VertexShader *s, D3D8VSProgram *p) {
  unsigned pc = 1;
  p->count = 0;
  if (!decode_inputs(s, p->input, &p->input_end))
    return 0;
  while (pc < s->function_dwords) {
    uint32_t op = s->function[pc++] & 0xffffu, d;
    unsigned nsrc = source_count(op), i;
    D3D8VSInstruction *insn;
    int dst;
    if (op == 0xffffu)
      return 1;
    if (!nsrc) {
      x2_log_error("d3d8: VS 1.1 opcode %u at dword %u is not "
                   "implemented; the draw is refused.\n",
                   op, pc - 1);
      return 0;
    }
    if (pc + nsrc >= s->function_dwords) {
      x2_log_error("d3d8: VS 1.1 opcode %u at dword %u runs past the "
                   "program's %u dword(s).\n",
                   op, pc - 1, s->function_dwords);
      return 0;
    }
    if (p->count == VS_MAX_INSTRUCTIONS) {
      x2_log_error("d3d8: VS 1.1 program has more than the %u instruction "
                   "slots the model allows.\n",
                   VS_MAX_INSTRUCTIONS);
      return 0;
    }
    insn = &p->insn[p->count];
    d = s->function[pc++];
    for (i = 0; i < nsrc; ++i)
      if (!decode_source(s->function[pc++], &insn->src[i]))
        return 0;
    dst = writable_register(reg_type(d), d & 0x7ffu);
    if (dst < 0) {
      x2_log_error("d3d8: VS 1.1 destination register type %u number %u is "
                   "unsupported.\n",
                   reg_type(d), d & 0x7ffu);
      return 0;
    }
    insn->op = (uint8_t)op;
    insn->dst = (uint16_t)dst;
    insn->mask = (uint8_t)((d >> 16) & 0xfu);
    if (!insn->mask)
      insn->mask = 0xf;
    p->count++;
  }
  return 1;
}

/* The decoded program, decoding it now if it has not been. A program that
   cannot run is decoded again at every draw, so every refusal says why. */
static const D3D8VSProgram *program(D3D8VertexShader *s) {
  if (s->program.state != 1)
    s->program.state = decode_program(s, &s->program) ? 1 : -1;
  return s->program.state == 1 ? &s->program : NULL;
}

static int read_source(const Vec *f, const D3D8VSSource *src, Vec *v) {
  const Vec *raw = &f[src->reg];
  unsigned i;
  if (src->relative) {
    int idx = (int)src->reg + (int)floorf(f[FILE_ADDR].x[0] + 0.5f);
    if (idx < 0 || idx >= VS_CONSTANTS) {
      lucent_log_warn(
          "d3d8",
          "VS 1.1 indexed constant c[%d] is outside the %d-register "
          "file; draw refused",
          idx, VS_CONSTANTS);
      return 0;
    }
    raw = &f[FILE_CONST + idx];
  }
  for (i = 0; i < 4; ++i)
    v->x[i] = raw->x[src->swizzle[i]];
  if (src->negate)
    for (i = 0; i < 4; ++i)
      v->x[i] = -v->x[i];
  return 1;
}

static int execute_one(const D3D8VSProgram *p, Vec f[FILE_SIZE]) {
  unsigned k;
  for (k = 0; k < p->count; ++k) {
    const D3D8VSInstruction *insn = &p->insn[k];
    Vec x, y, z, v;
    unsigned i;
    if (!read_source(f, &insn->src[0], &x))
      return 0;
    switch (insn->op) {
    case OP_MOV:
      v = x;
      break;
    case OP_ADD:
    case OP_SUB:
    case OP_MUL:
      if (!read_source(f, &insn->src[1], &y))
        return 0;
      for (i = 0; i < 4; i++)
        v.x[i] = insn->op == OP_ADD   ? x.x[i] + y.x[i]
                 : insn->op == OP_SUB ? x.x[i] - y.x[i]
                                      : x.x[i] * y.x[i];
      break;
    case OP_MAD:
      if (!read_source(f, &insn->src[1], &y) ||
          !read_source(f, &insn->src[2], &z))
        return 0;
      for (i = 0; i < 4; i++)
        v.x[i] = x.x[i] * y.x[i] + z.x[i];
      break;
    default: /* DP3, DP4: decode_program admits nothing else */
    {
      float dot = 0;
      unsigned n = insn->op == OP_DP3 ? 3 : 4;
      if (!read_source(f, &insn->src[1], &y))
        return 0;
      for (i = 0; i < n; i++)
        dot += x.x[i] * y.x[i];
      for (i = 0; i < 4; i++)
        v.x[i] = dot;
      break;
    }
    }
    for (i = 0; i < 4; ++i)
      if (insn->mask & (1u << i))
        f[insn->dst].x[i] = v.x[i];
  }
  return 1;
}

int d3d8_vs_execute(uint32_t handle, const float constants[VS_CONSTANTS][4],
                    const void *vertices, uint32_t vertex_bytes,
                    uint32_t stride, uint32_t first, uint32_t count,
                    D3D8VSOutput *output) {
  D3D8VertexShader *s = d3d8_vs_get(handle, "draw");
  const D3D8VSProgram *p;
  Vec f[FILE_SIZE];
  unsigned v, i;
  const auto *base = static_cast<const uint8_t *>(vertices);
  if (!s || !vertices || !stride || !output || !(p = program(s)))
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
  memcpy(&f[FILE_CONST], constants, VS_CONSTANTS * sizeof(Vec));
  for (v = 0; v < count; ++v) {
    const uint8_t *src = base + (first + v) * stride;
    /* Every register but the constants starts a vertex at zero, and oD0 at
       one. */
    memset(f, 0, FILE_CONST * sizeof(Vec));
    f[OUT_D0].x[0] = f[OUT_D0].x[1] = f[OUT_D0].x[2] = f[OUT_D0].x[3] = 1.0f;
    for (i = 0; i < VS_INPUTS; ++i)
      if (p->input[i].present)
        f[FILE_INPUT + i] =
            load_input(src + p->input[i].offset, p->input[i].type);
    if (!execute_one(p, f))
      return 0;
    memcpy(output[v].position, f[OUT_POS].x, sizeof output[v].position);
    memcpy(output[v].diffuse, f[OUT_D0].x, sizeof output[v].diffuse);
    output[v].texcoord[0] = f[OUT_T0].x[0];
    output[v].texcoord[1] = f[OUT_T0].x[1];
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
  struct {
    float p[4], selector, colour[4];
  } vertex = {{1, 2, 3, 1}, 1, {0.25f, 0.5f, 0.75f, 1.0f}};
  float c[VS_CONSTANTS][4] = {{0}};
  D3D8VSOutput out;
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

  bad = d3d8_vs_create(decl, bad_code, 0);
  if (!bad || d3d8_vs_execute(bad, c, &vertex, sizeof vertex, sizeof vertex, 0,
                              1, &out)) {
    x2_log_info("d3d8 VS selftest: FAILED -- unsupported opcode 0x1234 was not "
                "refused by the shipping executor.\n");
    fails++;
  }
  if (bad)
    d3d8_vs_delete(bad);
  x2_log_info("d3d8 VS selftest: %s -- positive relative-addressed program and "
              "negative unsupported-opcode program both exercised\n",
              fails ? "FAILED" : "PASSED");
  return fails;
}
