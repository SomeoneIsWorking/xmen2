#include "thumb_stick.h"

#include <algorithm>
#include <cmath>

namespace x2::input {

void ThumbStick::set_travel(float travel) {
  travel_ = travel > 0.0F ? travel : 0.0F;
}

void ThumbStick::release() {
  engaged_ = false;
  deflection_ = {};
}

ThumbStick::Deflection ThumbStick::track(const lucent::touch::Event &event) {
  const bool released = event.phase == lucent::touch::Phase::ended ||
                        event.phase == lucent::touch::Phase::canceled;
  if (released || travel_ <= 0.0F) {
    release();
    return deflection_;
  }

  if (!engaged_ || event.phase == lucent::touch::Phase::began) {
    engaged_ = true;
    centre_ = event.origin;
  }
  float x = (event.position.x - centre_.x) / travel_;
  float y = (event.position.y - centre_.y) / travel_;
  float reach = std::hypot(x, y);
  if (reach > 1.0F) {
    /* Drag the centre so the thumb sits on the rim. */
    const float excess = (reach - 1.0F) / reach;
    centre_.x += x * travel_ * excess;
    centre_.y += y * travel_ * excess;
    x /= reach;
    y /= reach;
    reach = 1.0F;
  }
  if (reach <= kDeadZone) {
    deflection_ = {};
    return deflection_;
  }

  /* Clamp to the circle rather than to each axis on its own: a square clamp
     lets a corner reach 1.0 in BOTH axes, so the diagonals run faster than
     any direction the stick can otherwise produce. */
  const float steered =
      std::min((reach - kDeadZone) / (1.0F - kDeadZone), 1.0F) / reach;
  deflection_ = {x * steered, y * steered};
  return deflection_;
}

} // namespace x2::input
