/*
 * The decoded VS 1.1 program, packed for the GPU's interpreter
 * (src/gpu/shaders/vs11_program.glsl, issue #187).
 *
 * Packing is a re-encoding of what d3d8_vs_decode.cpp already decided --
 * flat registers, swizzle indices, input offsets -- into the uniform block's
 * words, done once per shader and kept beside the decoded program.
 */
#include "d3d8_vertex_shader_internal.h"

#include <lucent/log_c.h>

static_assert(D3D8_MAX_VS_CONSTANTS == GPU_VS_CONSTANTS,
              "the GPU program reads the device's whole constant file");
static_assert(VS_MAX_INSTRUCTIONS == GPU_VS_MAX_INSTRUCTIONS,
              "the GPU program holds every instruction VS 1.1 allows");
static_assert((int)VS_FILE_CONST == (int)GPU_VS_REG_CONST &&
                  (int)VS_FILE_ADDR == (int)GPU_VS_REG_ADDR &&
                  (int)VS_OUT_POS == (int)GPU_VS_REG_OUT_POS &&
                  (int)VS_OUT_D0 == (int)GPU_VS_REG_OUT_D0 &&
                  (int)VS_OUT_T0 == (int)GPU_VS_REG_OUT_T0 &&
                  (int)VS_FILE_INPUT == (int)GPU_VS_REG_INPUT,
              "the decoder and the GPU program name one flat register file");
static_assert((int)VS_OP_MOV == (int)GPU_VS_OP_MOV &&
                  (int)VS_OP_ADD == (int)GPU_VS_OP_ADD &&
                  (int)VS_OP_SUB == (int)GPU_VS_OP_SUB &&
                  (int)VS_OP_MAD == (int)GPU_VS_OP_MAD &&
                  (int)VS_OP_MUL == (int)GPU_VS_OP_MUL &&
                  (int)VS_OP_DP3 == (int)GPU_VS_OP_DP3 &&
                  (int)VS_OP_DP4 == (int)GPU_VS_OP_DP4,
              "the GPU program runs every opcode the decoder accepts");

namespace {

/* D3DVSDT_* -> the GPU's input type; ABSENT for one the GPU cannot fetch. */
GpuVsInputType input_type(unsigned d3d_type) {
  switch (d3d_type) {
  case 0:
    return GPU_VS_INPUT_FLOAT1;
  case 1:
    return GPU_VS_INPUT_FLOAT2;
  case 2:
    return GPU_VS_INPUT_FLOAT3;
  case 3:
    return GPU_VS_INPUT_FLOAT4;
  case 4:
    return GPU_VS_INPUT_D3DCOLOR;
  case 5:
    return GPU_VS_INPUT_UBYTE4;
  default: /* SHORT2 and SHORT4: no float format keeps -32768 exact */
    return GPU_VS_INPUT_ABSENT;
  }
}

uint32_t source_word(const D3D8VSSource &src) {
  uint32_t swizzle = 0;
  for (unsigned c = 0; c < 4; ++c) {
    swizzle |= (uint32_t)(src.swizzle[c] & 3u) << (2u * c);
  }
  return (uint32_t)src.reg | swizzle << GPU_VS_SOURCE_SWIZZLE_SHIFT |
         (src.negate ? (uint32_t)GPU_VS_SOURCE_NEGATE : 0u) |
         (src.relative ? (uint32_t)GPU_VS_SOURCE_RELATIVE : 0u);
}

/* Pack `p` into `out`; 0, having said why, for a program the GPU cannot run. */
int pack(uint32_t handle, const D3D8VSProgram &p, GpuVsProgram *out) {
  for (unsigned i = GPU_VS_INPUTS; i < VS_INPUTS; ++i) {
    if (p.input[i].present) {
      lucent_log_info("d3d8",
                      "VS 0x%08x declares input v%u; the GPU program "
                      "has %u, so its draws run on the CPU executor",
                      handle, i, (unsigned)GPU_VS_INPUTS);
      return 0;
    }
  }
  for (unsigned i = 0; i < GPU_VS_INPUTS; ++i) {
    const D3D8VSInput &in = p.input[i];
    const GpuVsInputType type =
        in.present ? input_type(in.type) : GPU_VS_INPUT_ABSENT;
    if (in.present && type == GPU_VS_INPUT_ABSENT) {
      lucent_log_info("d3d8",
                      "VS 0x%08x input v%u has type %u, which the GPU "
                      "program cannot fetch, so its draws run on the CPU "
                      "executor",
                      handle, i, (unsigned)in.type);
      return 0;
    }
    gpu_vs_program_set_input(out, i, type, in.offset);
  }
  for (unsigned k = 0; k < p.count; ++k) {
    const D3D8VSInstruction &insn = p.insn[k];
    const unsigned nsrc = d3d8_vs_source_count(insn.op);
    uint32_t *word = out->block.insn[k];
    word[0] = (uint32_t)insn.op | (uint32_t)(insn.mask & 0xFu) << 8 |
              (uint32_t)insn.dst << 16;
    for (unsigned s = 0; s < 3; ++s) {
      /* An unused operand repeats the first, as the executor's does. */
      word[1 + s] = source_word(insn.src[s < nsrc ? s : 0]);
    }
  }
  out->block.count = p.count;
  return 1;
}

} // namespace

const GpuVsProgram *d3d8_vs_gpu_program(uint32_t handle, uint32_t *input_end) {
  D3D8VertexShader *s = d3d8_vs_get(handle, "draw");
  const D3D8VSProgram *p;
  if (!s || !(p = d3d8_vs_program(s))) {
    return nullptr;
  }
  if (s->gpu_state == 0) {
    s->gpu_state = pack(handle, *p, &s->gpu) ? 1 : -1;
  }
  *input_end = p->input_end;
  return s->gpu_state == 1 ? &s->gpu : nullptr;
}
