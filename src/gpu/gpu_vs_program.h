#ifndef GPU_VS_PROGRAM_H
#define GPU_VS_PROGRAM_H

/*
 * A D3D8 VS 1.1 program in the form the GPU runs it (issue #187).
 *
 * The device decodes the guest's program once and packs it here
 * (src/d3d8/d3d8_vs_gpu.cpp); a draw carries a pointer to it and to the
 * device's constant file, and the draw path pushes both as uniform blocks in
 * front of src/gpu/shaders/vs11_program.glsl, which interprets it per vertex.
 * The guest's vertex buffer is bound as it is: no CPU pass over the vertices,
 * no buffer made per draw.
 *
 * This header is the GPU side's whole knowledge of the program. It names no
 * D3D8 type, so the d3d8 layer depends on it and not the other way round.
 */
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
  GPU_VS_MAX_INSTRUCTIONS = 128,
  /* Attribute locations; a WebGPU pipeline has sixteen. */
  GPU_VS_INPUTS = 16,
  GPU_VS_CONSTANTS = 256
};

/* How one input register is fed from the vertex. The shader's vs11_input
   converts each to the value the CPU executor's load_input gives. */
typedef enum {
  GPU_VS_INPUT_ABSENT = 0,
  GPU_VS_INPUT_FLOAT1 = 1,
  GPU_VS_INPUT_FLOAT2 = 2,
  GPU_VS_INPUT_FLOAT3 = 3,
  GPU_VS_INPUT_FLOAT4 = 4,
  GPU_VS_INPUT_D3DCOLOR = 5,
  GPU_VS_INPUT_UBYTE4 = 6
} GpuVsInputType;

/* The flat register file an instruction names: r0..r11, v0..v15, a0, oPos,
   oFog, oPts, oD0, oD1, oT0..oT7, then the constants. */
enum {
  GPU_VS_REG_TEMP = 0,
  GPU_VS_REG_INPUT = 12,
  GPU_VS_REG_ADDR = 29,
  GPU_VS_REG_OUT_POS = 30,
  GPU_VS_REG_OUT_D0 = 33,
  GPU_VS_REG_OUT_T0 = 35,
  GPU_VS_REG_CONST = 43
};

/* The opcodes the shader runs, numbered as the D3D8 token stream numbers
   them. */
enum {
  GPU_VS_OP_MOV = 1,
  GPU_VS_OP_ADD = 2,
  GPU_VS_OP_SUB = 3,
  GPU_VS_OP_MAD = 4,
  GPU_VS_OP_MUL = 5,
  GPU_VS_OP_DP3 = 8,
  GPU_VS_OP_DP4 = 9
};

/* Source-operand word fields; vs11_program.glsl decodes the same ones. */
enum {
  GPU_VS_SOURCE_SWIZZLE_SHIFT = 9,
  GPU_VS_SOURCE_NEGATE = 1u << 17,
  GPU_VS_SOURCE_RELATIVE = 1u << 18
};

/*
 * The Vs11Program uniform block, std140, field for field. Each instruction is
 * (op | mask << 8 | dst << 16, source 0, source 1, source 2).
 */
typedef struct {
  uint32_t insn[GPU_VS_MAX_INSTRUCTIONS][4];
  uint32_t input_type[GPU_VS_INPUTS]; /* GpuVsInputType */
  uint32_t count;
  uint32_t pad[3];
} GpuVsProgramBlock;

/* The inputs as pipeline state: a pipeline is keyed on this by value. An
   absent input has type GPU_VS_INPUT_ABSENT and offset 0. */
typedef struct {
  uint8_t type[GPU_VS_INPUTS]; /* GpuVsInputType */
  uint16_t offset[GPU_VS_INPUTS];
} GpuVsInputLayout;

typedef struct {
  GpuVsProgramBlock block;
  GpuVsInputLayout inputs;
} GpuVsProgram;

/* Feed input `index` from `offset` bytes into the vertex as `type`: the
   pipeline's layout and the shader's copy of the type, together. */
void gpu_vs_program_set_input(GpuVsProgram *program, unsigned index,
                              GpuVsInputType type, uint16_t offset);

#ifdef __cplusplus
}
#endif

#ifdef X2_WITH_SDL
#include <SDL3/SDL.h>

/* The attribute for each of the shader's sixteen inputs. An absent input
   reads one float at offset 0, which every vertex has; the shader zeroes it
   by type. */
void gpu_vs_program_attributes(const GpuVsInputLayout *inputs,
                               SDL_GPUVertexAttribute out[GPU_VS_INPUTS]);

/* Push the program into vertex uniform slot 1 and the constant file --
   the Vs11Constants block, the device's whole file -- into
   slot 2. */
void gpu_vs_program_push(SDL_GPUCommandBuffer *command,
                         const GpuVsProgram *program,
                         const float constants[GPU_VS_CONSTANTS][4]);
#endif

#endif /* GPU_VS_PROGRAM_H */
