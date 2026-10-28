#include "touch_targets.hpp"

#include <utility>

namespace x2::input {
namespace {

TouchTargets targets;

/* Squared distance from `point` to the middle of `box`. */
float to_middle(const SceneBox &box, presentation::ScenePoint point) {
  const float dx = point.x - 0.5F * (box.left + box.right);
  const float dz = point.z - 0.5F * (box.bottom + box.top);
  return dx * dx + dz * dz;
}

bool same(const SceneBox &a, const SceneBox &b) {
  return a.left == b.left && a.right == b.right && a.bottom == b.bottom &&
         a.top == b.top;
}

} // namespace

void TouchTargets::put(const TouchTarget &target) {
  drawing_.push_back(target);
}

void TouchTargets::next_frame() { shown_ = std::exchange(drawing_, {}); }

std::optional<presentation::ScenePoint>
TouchTargets::press(presentation::ScenePoint point) {
  held_.reset();
  const TouchTarget *hit = nullptr;
  for (const TouchTarget &target : shown_) {
    if (target.area.contains(point) &&
        (hit == nullptr ||
         to_middle(target.area, point) < to_middle(hit->area, point))) {
      hit = &target;
    }
  }
  if (hit == nullptr) {
    return std::nullopt;
  }
  held_ = hit->area;
  return hit->aim;
}

void TouchTargets::release() { held_.reset(); }

bool TouchTargets::pressed(const SceneBox &area) const {
  return held_ && same(*held_, area);
}

TouchTargets &touch_targets() { return targets; }

} // namespace x2::input
