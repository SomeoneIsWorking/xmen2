#include "thumb_stick.h"

#include <algorithm>
#include <cmath>

namespace x2::input {

void ThumbStick::set_travel(float travel) {
  travel_ = travel > 0.0F ? travel : 0.0F;
  deflection_ = {};
}

void ThumbStick::release() { deflection_ = {}; }

ThumbStick::Deflection ThumbStick::track(const lucent::touch::Event &event) {
  const bool released = event.phase == lucent::touch::Phase::ended ||
                        event.phase == lucent::touch::Phase::canceled;
  if (released || travel_ <= 0.0F) {
    release();
    return deflection_;
  }

  const float x = (event.position.x - event.origin.x) / travel_;
  const float y = (event.position.y - event.origin.y) / travel_;
  const float reach = std::hypot(x, y);
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
