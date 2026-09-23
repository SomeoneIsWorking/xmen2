/*
 * The VS 1.1 decoder: a shader's declaration and token stream, turned once
 * into the D3D8VSProgram the executor in d3d8_vs_execute.cpp runs.
 *
 * It is the part that has to be RIGHT about the encoding -- register types,
 * relative addressing, source modifiers and write masks -- and it REFUSES,
 * with the reason logged, whatever it cannot represent rather than decoding
 * it as something else.
 */
#include "d3d8_vertex_shader_internal.h"

#include "../native/x2_log.h"

#include <cstring>
#include <lucent/log_c.h>

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

/* The flat register a writable register token names, or -1. */
static int writable_register(unsigned type, unsigned n) {
  if (type == 0 && n < 12)
    return VS_FILE_TEMP + (int)n;
  if (type == 3 && n == 0)
    return VS_FILE_ADDR;
  if (type == 4 && n < 3)
    return VS_FILE_OUT + (int)n;
  if (type == 5 && n < 2)
    return VS_FILE_OUT + 3 + (int)n;
  if (type == 6 && n < 8)
    return VS_FILE_OUT + 5 + (int)n;
  return -1;
}

static int decode_source(uint32_t t, D3D8VSSource *src) {
  unsigned type = reg_type(t), n = t & 0x7ffu, i;
  int reg = -1;
  memset(src, 0, sizeof *src);
  if (type == 1 && n < VS_INPUTS) {
    reg = VS_FILE_INPUT + (int)n;
  } else if (type == 2) {
    if (t & 0x00002000u) {
      src->relative = 1;
      reg = (int)n; /* the base; the constant is chosen per vertex */
    } else if (n < VS_CONSTANTS) {
      reg = VS_FILE_CONST + (int)n;
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

unsigned d3d8_vs_source_count(unsigned op) {
  switch (op) {
  case VS_OP_MOV:
    return 1;
  case VS_OP_ADD:
  case VS_OP_SUB:
  case VS_OP_MUL:
  case VS_OP_DP3:
  case VS_OP_DP4:
    return 2;
  case VS_OP_MAD:
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
    unsigned nsrc = d3d8_vs_source_count(op), i;
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

const D3D8VSProgram *d3d8_vs_program(D3D8VertexShader *s) {
  if (s->program.state != 1)
    s->program.state = decode_program(s, &s->program) ? 1 : -1;
  return s->program.state == 1 ? &s->program : NULL;
}
