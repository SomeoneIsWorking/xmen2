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

void TouchTargets::next_frame() {
  shown_ = std::exchange(drawing_, {});
  tapped_.reset();
}

std::optional<TouchTarget> TouchTargets::press(presentation::ScenePoint point) {
  held_.reset();
  const TouchTarget *hit = nullptr;
  for (const TouchTarget &target : shown_) {
    if (target.area.contains(point) &&
        (hit == nullptr ||
         to_middle(target.area, point) < to_middle(hit->area, point))) {
      hit = &target;
    }
  }
  if (hit != nullptr) {
    held_ = *hit;
  }
  return held_;
}

void TouchTargets::release(presentation::ScenePoint point) {
  if (held_ && held_->press == TouchPress::own && held_->area.contains(point)) {
    tapped_ = held_->area;
  }
  held_.reset();
}

bool TouchTargets::pressed(const SceneBox &area) const {
  return held_ && same(held_->area, area);
}

bool TouchTargets::take_tap(const SceneBox &area) {
  if (!tapped_ || !same(*tapped_, area)) {
    return false;
  }
  tapped_.reset();
  return true;
}

void TouchTargets::hover(presentation::ScenePoint aim) { hover_ = aim; }

std::optional<presentation::ScenePoint> TouchTargets::take_hover() {
  return std::exchange(hover_, std::nullopt);
}

TouchTargets &touch_targets() { return targets; }

} // namespace x2::input
