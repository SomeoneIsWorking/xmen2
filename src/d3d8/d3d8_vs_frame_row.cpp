#include "d3d8_vs_frame_row.h"

#include "../native/x2_log.h"
#include "d3d8_vertex_shader.h"
#include "guest_memory.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace x2::d3d8 {
namespace {

inline constexpr uint32_t kMaxVertices = 65536u;

struct Range {
  float low = 1e30f;
  float high = -1e30f;

  void add(float value) {
    low = std::min(low, value);
    high = std::max(high, value);
  }
};

float luma(const float *colour) {
  return 0.299f * colour[0] + 0.587f * colour[1] + 0.114f * colour[2];
}

} // namespace

void d3d8_vs_frame_row(unsigned long draw, const D3D8State *s,
                       const D3D8DrawRequest *req, const GpuDraw *out) {
  uint32_t first =
      req->num_vertices ? req->base_vertex + req->min_index : req->first_vertex;
  uint32_t count = req->num_vertices ? req->num_vertices
                                     : req->vertex_bytes / req->stride - first;
  count = std::min(count, kMaxVertices);
  std::vector<D3D8VSOutput> vertices(count);
  if (!count || !req->vertex_guest_bytes ||
      !d3d8_vs_execute(s->vertex_shader, s->vertex_shader_constant,
                       guest_memory_const_pointer(req->vertex_guest_bytes),
                       req->vertex_bytes, req->stride, first, count,
                       vertices.data())) {
    x2_log_error("  draw %-4lu VS 0x%08x stride %u: the CPU executor could "
                 "not run vertices %u..+%u\n",
                 draw, s->vertex_shader, req->stride, first, count);
    return;
  }
  Range x, y, diffuse, alpha, u, v;
  uint32_t behind = 0;
  for (const D3D8VSOutput &vertex : vertices) {
    diffuse.add(luma(vertex.diffuse));
    alpha.add(vertex.diffuse[3]);
    u.add(vertex.texcoord[0]);
    v.add(vertex.texcoord[1]);
    if (vertex.position[3] <= 0.0f) {
      behind++;
      continue;
    }
    x.add((vertex.position[0] / vertex.position[3] * 0.5f + 0.5f) * 800.0f);
    y.add((0.5f - vertex.position[1] / vertex.position[3] * 0.5f) * 600.0f);
  }
  x2_log_error("  draw %-4lu %5u prim  VS 0x%08x stride %-3u tex %-3u  "
               "screen x %5.0f..%-5.0f y %5.0f..%-5.0f  %u behind  diffuse "
               "luma %.3f..%.3f alpha %.3f..%.3f  uv %.3f..%.3f x %.3f..%.3f\n",
               draw, out->prim_count, s->vertex_shader, req->stride,
               out->texture, x.low, x.high, y.low, y.high, behind, diffuse.low,
               diffuse.high, alpha.low, alpha.high, u.low, u.high, v.low,
               v.high);
}

} // namespace x2::d3d8
