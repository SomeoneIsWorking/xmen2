/* D3D8 programmable vertex-shader objects and their original token streams. */
#ifndef D3D8_VERTEX_SHADER_H
#define D3D8_VERTEX_SHADER_H

#include <stddef.h>

#include "d3d8_state.h"
#include <stdint.h>

#define D3D8_VS_CONSTANTS 96

#ifdef __cplusplus
extern "C" {
#endif

typedef struct D3D8VertexShader D3D8VertexShader;

uint32_t d3d8_vs_create(const uint32_t *declaration, const uint32_t *function,
                        uint32_t usage);
D3D8VertexShader *d3d8_vs_get(uint32_t handle, const char *operation);
int d3d8_vs_delete(uint32_t handle);
void d3d8_vs_reset(void);

const uint32_t *d3d8_vs_declaration(const D3D8VertexShader *shader,
                                    size_t *bytes);
const uint32_t *d3d8_vs_function(const D3D8VertexShader *shader, size_t *bytes);

typedef struct {
  float position[4];
  float diffuse[4];
  float texcoord[2];
} D3D8VSOutput;

/* Execute the original VS 1.1 program over host-visible vertex bytes. */
int d3d8_vs_execute(uint32_t handle,
                    const float constants[D3D8_MAX_VS_CONSTANTS][4],
                    const void *vertices, uint32_t vertex_bytes,
                    uint32_t stride, uint32_t first, uint32_t count,
                    D3D8VSOutput *output);

void d3d8_vs_report(void);
int d3d8_vs_selftest(void);

/* Every distinct value SetVertexShader received, with counts -- defined in
   d3d8_device.c, where the call lives. D3D8 overloads the argument as an FVF
   code OR a shader handle, told apart by bit 0 (D3DFVF_RESERVED0). */
void d3d8_vertex_shader_binding_report(void);

/* The same census as one heartbeat line; a '*' marks a shader handle. */
void d3d8_vertex_shader_binding_line(char *buf, size_t n);

#ifdef __cplusplus
}
#endif

#endif
