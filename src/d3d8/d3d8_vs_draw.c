/* See d3d8_vs_draw.h. */
#include "d3d8_vs_draw.h"

#include "../native/x2_log.h"
#include "d3d8_vertex_shader.h"
#include "guest_memory.h"

#include <stddef.h>
#include <stdlib.h>

static unsigned long g_gpu_draws, g_gpu_vertices;

void d3d8_vs_draw_gpu_counts(unsigned long *draws, unsigned long *vertices) {
  *draws = g_gpu_draws;
  *vertices = g_gpu_vertices;
}

/*
 * The GPU runs the program over the guest's own vertex buffer. The layout
 * fields say what the program hands the vertex stage -- a position, a diffuse
 * colour and a texture coordinate, no normal or specular -- exactly as they
 * did for the executor's output buffer, so the stage treats both alike.
 */
static int gpu_source(const D3D8State *s, const D3D8DrawRequest *req,
                      const GpuVsProgram *program, uint32_t input_end,
                      GpuDraw *out) {
  if (!req->stride) {
    x2_log_error("d3d8: programmable draw has a stream-0 stride of zero.\n");
    return 0;
  }
  if (input_end > req->stride) {
    x2_log_error("d3d8: the vertex declaration reads to byte %u, past "
                 "stride %u.\n",
                 input_end, req->stride);
    return 0;
  }
  out->vertex_stride = req->stride;
  out->vs_program = program;
  out->vs_constants = s->vertex_shader_constant;
  out->pos_offset = 0;
  out->pretransformed = 0;
  out->programmable = 1;
  out->color_offset = 0;
  out->specular_offset = -1;
  out->uv_offset = 0;
  out->normal_offset = -1;
  g_gpu_draws++;
  g_gpu_vertices += req->vertex_bytes / req->stride;
  return 1;
}

/* The CPU executor runs the program over the guest's vertex bytes, into a
   buffer made and uploaded for this draw. */
static int cpu_source(const D3D8State *s, const D3D8DrawRequest *req,
                      GpuDraw *out) {
  D3D8VSOutput *vertices;
  uint32_t count, bytes;
  if (!req->vertex_guest_bytes || !req->vertex_bytes || !req->stride) {
    x2_log_error("d3d8: programmable draw has no host-visible "
                 "stream-0 bytes (guest=0x%08x bytes=%u stride=%u).\n",
                 req->vertex_guest_bytes, req->vertex_bytes, req->stride);
    return 0;
  }
  count = req->vertex_bytes / req->stride;
  if (!count || count > UINT32_MAX / sizeof *vertices) {
    x2_log_error("d3d8: programmable draw derives %u vertices "
                 "from %u bytes at stride %u.\n",
                 count, req->vertex_bytes, req->stride);
    return 0;
  }
  bytes = count * (uint32_t)sizeof *vertices;
  vertices = malloc(bytes);
  if (!vertices) {
    return 0;
  }
  if (!d3d8_vs_execute(s->vertex_shader, s->vertex_shader_constant,
                       guest_memory_const_pointer(req->vertex_guest_bytes),
                       req->vertex_bytes, req->stride, 0, count, vertices)) {
    free(vertices);
    return 0;
  }
  out->vertices = gpu_buffer_create(GPU_BUF_VERTEX, bytes);
  if (!out->vertices || !gpu_buffer_upload(out->vertices, 0, vertices, bytes)) {
    if (out->vertices) {
      gpu_buffer_destroy(out->vertices);
    }
    out->vertices = 0;
    free(vertices);
    return 0;
  }
  free(vertices);
  out->owns_vertices = 1;
  out->vs_program = NULL;
  out->vs_constants = NULL;
  out->vertex_stride = sizeof(D3D8VSOutput);
  out->pos_offset = offsetof(D3D8VSOutput, position);
  out->pretransformed = 0;
  out->programmable = 1;
  out->color_offset = offsetof(D3D8VSOutput, diffuse);
  out->specular_offset = -1;
  out->uv_offset = offsetof(D3D8VSOutput, texcoord);
  out->normal_offset = -1;
  return 1;
}

int d3d8_vs_draw_source(const D3D8State *s, const D3D8DrawRequest *req,
                        GpuDraw *out) {
  uint32_t input_end = 0;
  const GpuVsProgram *program =
      d3d8_vs_gpu_program(s->vertex_shader, &input_end);
  return program ? gpu_source(s, req, program, input_end, out)
                 : cpu_source(s, req, out);
}

int d3d8_vs_draw_source_on(const D3D8State *s, const D3D8DrawRequest *req,
                           D3D8VsExecutor executor, GpuDraw *out) {
  uint32_t input_end = 0;
  const GpuVsProgram *program;
  if (executor == D3D8_VS_ON_CPU) {
    return cpu_source(s, req, out);
  }
  program = d3d8_vs_gpu_program(s->vertex_shader, &input_end);
  return program && gpu_source(s, req, program, input_end, out);
}
