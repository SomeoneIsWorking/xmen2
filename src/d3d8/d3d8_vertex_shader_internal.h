#pragma once

/*
 * The shader object's layout, shared by its two owners: the handle store in
 * d3d8_vertex_shader.cpp and the VS 1.1 executor in d3d8_vs_execute.cpp.
 *
 * Private to those two. Everything outside holds an opaque handle and goes
 * through d3d8_vertex_shader.h, which is what makes the generation counter in
 * the handle worth having.
 */
#include "d3d8_vertex_shader.h"

#include "d3d8_state.h"
#include "gpu_vs_program.h"
#include <cstdint>

namespace x2::d3d8 {

inline constexpr int VS_MAX = 64;
inline constexpr unsigned VS_HANDLE_BASE = 0xf0000001u;
inline constexpr int VS_DECL_MAX_DWORDS = 256;
inline constexpr int VS_CODE_MAX_DWORDS = 4096;
/*
 * The constant register file is D3D8_MAX_VS_CONSTANTS, not a 96 written here.
 *
 * 96 was hardcoded in five places in this file while D3DCAPS8 declared its own
 * number elsewhere, and the two were only ever equal by coincidence. When
 * MaxVertexShaderConst was raised to 256 to match the real driver, the engine
 * took the promise and indexed c[96] -- and this executor refused every one of
 * 1832 skinned draws a run, which is exactly the geometry the change was
 * meant to restore. A declared capability and the code that honours it must
 * be the same symbol.
 */
inline constexpr int VS_CONSTANTS = D3D8_MAX_VS_CONSTANTS;

/*
 * The program decoded once, by the executor, the first time it runs: register
 * numbers resolved to one flat register file, swizzles to indices, and the
 * declaration to input offsets. Decoding every token again for every vertex
 * was 7% of the Dead Zone route's samples. VS 1.1 allows 128 instruction
 * slots.
 */
inline constexpr int VS_MAX_INSTRUCTIONS = 128;
inline constexpr int VS_INPUTS = 17;

/*
 * The flat register file a decoded program addresses: temporaries, inputs,
 * a0, outputs (oPos, oFog, oPts, oD0-1, oT0-7) and then the constants, which
 * the executor reads from the caller's array rather than the file.
 */
inline constexpr int VS_FILE_TEMP = 0;
inline constexpr int VS_FILE_INPUT = VS_FILE_TEMP + 12;
inline constexpr int VS_FILE_ADDR = VS_FILE_INPUT + VS_INPUTS;
inline constexpr int VS_FILE_OUT = VS_FILE_ADDR + 1;
inline constexpr int VS_FILE_CONST = VS_FILE_OUT + 13;
inline constexpr int VS_OUT_POS = VS_FILE_OUT;
inline constexpr int VS_OUT_D0 = VS_FILE_OUT + 3;
inline constexpr int VS_OUT_T0 = VS_FILE_OUT + 5;

/* The VS 1.1 opcodes the executor implements. */
inline constexpr int VS_OP_MOV = 1;
inline constexpr int VS_OP_ADD = 2;
inline constexpr int VS_OP_SUB = 3;
inline constexpr int VS_OP_MAD = 4;
inline constexpr int VS_OP_MUL = 5;
inline constexpr int VS_OP_DP3 = 8;
inline constexpr int VS_OP_DP4 = 9;

struct D3D8VSSource {
  uint16_t reg; /* flat register, or the constant number when relative */
  uint8_t swizzle[4];
  uint8_t negate;
  uint8_t relative; /* c[reg + a0.x] */
};

struct D3D8VSInstruction {
  uint8_t op;
  uint8_t mask;
  uint16_t dst;
  D3D8VSSource src[3];
};

struct D3D8VSInput {
  uint8_t present;
  uint8_t type;
  uint16_t offset;
  uint16_t end; /* offset + the type's size */
};

/* One bit per register below the constant file. */
using D3D8VSRegisterSet = uint64_t;

struct D3D8VSProgram {
  /* 0 not yet decoded, 1 decoded, -1 refused (decoded again, and the reason
     logged again, at every draw that asks). */
  int state;
  /* The registers a vertex starts at zero: every one the program names that
     no input fills, and the outputs the executor stores whether or not the
     program writes them. oD0 starts at one instead, so it is not here. */
  D3D8VSRegisterSet zeroed;
  uint16_t count;
  uint16_t input_end; /* the furthest byte any input reads */
  D3D8VSInstruction insn[VS_MAX_INSTRUCTIONS];
  D3D8VSInput input[VS_INPUTS];
};

/* The decoded program, decoding it now if it has not been. A program that
   cannot run is decoded again at every draw, so every refusal says why. */
const D3D8VSProgram *d3d8_vs_program(D3D8VertexShader *shader);

/* How many source operands a VS 1.1 opcode takes; 0 for one the executor
   does not implement. */
unsigned d3d8_vs_source_count(unsigned op);

/* How much the executor did, for the store's run report. */
void d3d8_vs_execution_counts(unsigned long *draws, unsigned long *vertices);

} // namespace x2::d3d8

/* The definition of the opaque global declared in d3d8_vertex_shader.h. */
struct D3D8VertexShader {
  int used;
  uint16_t generation;
  uint32_t usage;
  uint32_t declaration[x2::d3d8::VS_DECL_MAX_DWORDS];
  uint32_t function[x2::d3d8::VS_CODE_MAX_DWORDS];
  uint16_t declaration_dwords;
  uint16_t function_dwords;
  x2::d3d8::D3D8VSProgram program;
  /* The program packed for the GPU (d3d8_vs_gpu.cpp): 0 not yet, 1 packed,
     -1 it has no form the GPU runs and draws take the CPU executor. */
  int gpu_state;
  x2::gpu::GpuVsProgram gpu;
};
