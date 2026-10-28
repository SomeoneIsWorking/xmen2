#include "touch_targets.hpp"

#include <utility>

namespace x2::input {
namespace {

TouchTargets targets;

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
  for (const TouchTarget &target : shown_) {
    if (target.area.contains(point)) {
      held_ = target.area;
      return target.aim;
    }
  }
  return std::nullopt;
}

void TouchTargets::release() { held_.reset(); }

bool TouchTargets::pressed(const SceneBox &area) const {
  return held_ && same(*held_, area);
}

TouchTargets &touch_targets() { return targets; }

} // namespace x2::input
