#include "retail_scene_plane.hpp"

#include "hud_layout.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace x2::presentation {

std::optional<RetailScenePlane>
RetailScenePlane::from_viewport(float aspect, float scale_x, float scale_z,
                                unsigned client_w, unsigned client_h) {
  if (client_w == 0u || client_h == 0u || !std::isfinite(aspect) ||
      !std::isfinite(scale_x) || !std::isfinite(scale_z)) {
    return std::nullopt;
  }
  const x2::presentation::HudSpace space =
      x2::presentation::hud_space(aspect, scale_x, scale_z);
  if (!(space.width >= 1.0F) || !(space.height >= 1.0F)) {
    return std::nullopt;
  }
  RetailScenePlane plane;
  plane.left_ = space.left;
  plane.width_ = space.width;
  plane.height_ = space.height;
  plane.client_w_ = client_w;
  plane.client_h_ = client_h;
  return plane;
}

ScenePoint RetailScenePlane::to_scene(ClientPoint client) const {
  // Truncating divisions, as the guest's integer arithmetic.
  const auto x = static_cast<std::int64_t>(client.x) *
                 static_cast<std::int64_t>(width_) /
                 static_cast<std::int64_t>(client_w_);
  const auto y = static_cast<std::int64_t>(client.y) *
                 static_cast<std::int64_t>(height_) /
                 static_cast<std::int64_t>(client_h_);
  return {left_ + static_cast<float>(x), height_ - static_cast<float>(y)};
}

ClientPoint RetailScenePlane::to_client(ScenePoint scene) const {
  const auto x = static_cast<std::int64_t>(std::ceil(
      (scene.x - left_) * static_cast<float>(client_w_) / std::trunc(width_)));
  const auto y = static_cast<std::int64_t>(
      std::ceil((height_ - scene.z) * static_cast<float>(client_h_) /
                std::trunc(height_)));
  return {static_cast<int>(std::clamp<std::int64_t>(
              x, 0, static_cast<std::int64_t>(client_w_) - 1)),
          static_cast<int>(std::clamp<std::int64_t>(
              y, 0, static_cast<std::int64_t>(client_h_) - 1))};
}

} // namespace x2::presentation
