#include "gpu_viewport.h"

#include <algorithm>

namespace x2::gpu {

namespace {

struct Request {
  int x = 0, y = 0, w = 0, h = 0;
  float minz = 0.0f, maxz = 1.0f;
  bool set = false;
};

Request g_request;

} // namespace

void gpu_viewport_request(int x, int y, int w, int h, float minz, float maxz) {
  g_request = Request{x, y, w, h, minz, maxz, true};
}

bool gpu_viewport_resolve(std::uint32_t target_w, std::uint32_t target_h,
                          GpuViewportRect *out) {
  const int tw = (int)target_w, th = (int)target_h;
  if (!g_request.set) {
    *out = GpuViewportRect{0.0f, 0.0f, (float)tw, (float)th, 0.0f, 1.0f};
    return tw > 0 && th > 0;
  }
  const int x = std::clamp(g_request.x, 0, std::max(tw, 0));
  const int y = std::clamp(g_request.y, 0, std::max(th, 0));
  const int w = std::min(std::max(g_request.w, 1), tw - x);
  const int h = std::min(std::max(g_request.h, 1), th - y);
  if (w < 1 || h < 1) {
    return false;
  }
  *out = GpuViewportRect{(float)x, (float)y,       (float)w,
                         (float)h, g_request.minz, g_request.maxz};
  return true;
}

} // namespace x2::gpu
