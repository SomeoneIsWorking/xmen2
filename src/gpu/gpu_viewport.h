/* The engine's requested viewport and its clamp to the target drawn into. */
#pragma once

#include <cstdint>

namespace x2::gpu {

struct GpuViewportRect {
  float x, y, w, h;
  float minz, maxz;
};

/* SetViewport's request in pixels, as the engine clamped it to its own
   render destination. */
void gpu_viewport_request(int x, int y, int w, int h, float minz, float maxz);

/* The request clamped to a target_w x target_h target, or the whole target
   before any request; false when the request misses the target. */
bool gpu_viewport_resolve(std::uint32_t target_w, std::uint32_t target_h,
                          GpuViewportRect *out);

} // namespace x2::gpu
