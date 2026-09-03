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

struct D3D8VertexShader {
  int used;
  uint16_t generation;
  uint32_t usage;
  uint32_t declaration[VS_DECL_MAX_DWORDS];
  uint32_t function[VS_CODE_MAX_DWORDS];
  uint16_t declaration_dwords;
  uint16_t function_dwords;
};

/* How much the executor did, for the store's run report. */
void d3d8_vs_execution_counts(unsigned long *draws, unsigned long *vertices);

#ifdef __cplusplus
}
#endif

#endif /* D3D8_VERTEX_SHADER_INTERNAL_H */
