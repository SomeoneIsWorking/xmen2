/* See gpu_vs_program.h. */
#include "gpu_vs_program.h"

#include <string.h>

_Static_assert(sizeof(GpuVsProgramBlock) == 128u * 16u + 4u * 16u + 16u,
               "GpuVsProgramBlock must match the std140 Vs11Program block");

void gpu_vs_program_set_input(GpuVsProgram *program, unsigned index,
                              GpuVsInputType type, uint16_t offset) {
  program->inputs.type[index] = (uint8_t)type;
  program->inputs.offset[index] = type == GPU_VS_INPUT_ABSENT ? 0u : offset;
  program->block.input_type[index] = (uint32_t)type;
}

#ifdef X2_WITH_SDL

static SDL_GPUVertexElementFormat input_format(uint32_t type) {
  switch (type) {
  case GPU_VS_INPUT_FLOAT2:
    return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
  case GPU_VS_INPUT_FLOAT3:
    return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
  case GPU_VS_INPUT_FLOAT4:
    return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
  case GPU_VS_INPUT_D3DCOLOR:
  case GPU_VS_INPUT_UBYTE4:
    /* Both arrive normalised; the shader reorders the one and scales the
       other back to its bytes. */
    return SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM;
  default: /* FLOAT1, and an absent input's placeholder */
    return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT;
  }
}

void gpu_vs_program_attributes(const GpuVsInputLayout *inputs,
                               SDL_GPUVertexAttribute out[GPU_VS_INPUTS]) {
  for (unsigned i = 0; i < GPU_VS_INPUTS; i++) {
    memset(&out[i], 0, sizeof out[i]);
    out[i].location = i;
    out[i].buffer_slot = 0;
    out[i].format = input_format(inputs->type[i]);
    out[i].offset = inputs->offset[i];
  }
}

void gpu_vs_program_push(SDL_GPUCommandBuffer *command,
                         const GpuVsProgram *program,
                         const float constants[GPU_VS_CONSTANTS][4]) {
  SDL_PushGPUVertexUniformData(command, 1, &program->block,
                               sizeof program->block);
  SDL_PushGPUVertexUniformData(command, 2, constants,
                               GPU_VS_CONSTANTS * 4u * sizeof(float));
}
#endif /* X2_WITH_SDL */
