#ifndef D3D8_VERTEX_SHADER_INTERNAL_H
#define D3D8_VERTEX_SHADER_INTERNAL_H

/*
 * The shader object's layout, shared by its two owners: the handle store in
 * d3d8_vertex_shader.c and the VS 1.1 executor in d3d8_vs_execute.cpp.
 *
 * Private to those two. Everything outside holds an opaque handle and goes
 * through d3d8_vertex_shader.h, which is what makes the generation counter in
 * the handle worth having.
 */
#include "d3d8_vertex_shader.h"

#include "d3d8_state.h"
#include "gpu_vs_program.h"
#include <stdint.h>

#define VS_MAX 64
#define VS_HANDLE_BASE 0xf0000001u
#define VS_DECL_MAX_DWORDS 256
#define VS_CODE_MAX_DWORDS 4096
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
#define VS_CONSTANTS D3D8_MAX_VS_CONSTANTS

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The program decoded once, by the executor, the first time it runs: register
 * numbers resolved to one flat register file, swizzles to indices, and the
 * declaration to input offsets. Decoding every token again for every vertex
 * was 7% of the Dead Zone route's samples. VS 1.1 allows 128 instruction
 * slots.
 */
#define VS_MAX_INSTRUCTIONS 128
#define VS_INPUTS 17

/*
 * The flat register file a decoded program addresses: temporaries, inputs,
 * a0, outputs (oPos, oFog, oPts, oD0-1, oT0-7) and then the constants, which
 * the executor reads from the caller's array rather than the file.
 */
enum {
  VS_FILE_TEMP = 0,
  VS_FILE_INPUT = VS_FILE_TEMP + 12,
  VS_FILE_ADDR = VS_FILE_INPUT + VS_INPUTS,
  VS_FILE_OUT = VS_FILE_ADDR + 1,
  VS_FILE_CONST = VS_FILE_OUT + 13,
};
enum {
  VS_OUT_POS = VS_FILE_OUT,
  VS_OUT_D0 = VS_FILE_OUT + 3,
  VS_OUT_T0 = VS_FILE_OUT + 5
};

/* The VS 1.1 opcodes the executor implements. */
enum {
  VS_OP_MOV = 1,
  VS_OP_ADD = 2,
  VS_OP_SUB = 3,
  VS_OP_MAD = 4,
  VS_OP_MUL = 5,
  VS_OP_DP3 = 8,
  VS_OP_DP4 = 9
};

typedef struct {
  uint16_t reg; /* flat register, or the constant number when relative */
  uint8_t swizzle[4];
  uint8_t negate;
  uint8_t relative; /* c[reg + a0.x] */
} D3D8VSSource;

typedef struct {
  uint8_t op;
  uint8_t mask;
  uint16_t dst;
  D3D8VSSource src[3];
} D3D8VSInstruction;

typedef struct {
  uint8_t present;
  uint8_t type;
  uint16_t offset;
  uint16_t end; /* offset + the type's size */
} D3D8VSInput;

/* One bit per register below the constant file. */
typedef uint64_t D3D8VSRegisterSet;

typedef struct {
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
} D3D8VSProgram;

struct D3D8VertexShader {
  int used;
  uint16_t generation;
  uint32_t usage;
  uint32_t declaration[VS_DECL_MAX_DWORDS];
  uint32_t function[VS_CODE_MAX_DWORDS];
  uint16_t declaration_dwords;
  uint16_t function_dwords;
  D3D8VSProgram program;
  /* The program packed for the GPU (d3d8_vs_gpu.cpp): 0 not yet, 1 packed,
     -1 it has no form the GPU runs and draws take the CPU executor. */
  int gpu_state;
  GpuVsProgram gpu;
};

/* The decoded program, decoding it now if it has not been. A program that
   cannot run is decoded again at every draw, so every refusal says why. */
const D3D8VSProgram *d3d8_vs_program(D3D8VertexShader *shader);

/* How many source operands a VS 1.1 opcode takes; 0 for one the executor
   does not implement. */
unsigned d3d8_vs_source_count(unsigned op);

/* How much the executor did, for the store's run report. */
void d3d8_vs_execution_counts(unsigned long *draws, unsigned long *vertices);

#ifdef __cplusplus
}
#endif

#endif /* D3D8_VERTEX_SHADER_INTERNAL_H */
