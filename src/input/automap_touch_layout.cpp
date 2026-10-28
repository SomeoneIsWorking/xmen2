#include "automap_touch_layout.hpp"

#include <cmath>

namespace x2::input {
namespace {

/* The update's hold: below the low bound is the low bound, else at most the
   high one. */
float held(float value, float low, float high) {
  if (value < low) {
    return low;
  }
  return value > high ? high : value;
}

} // namespace

MapPoint automap_dragged(const MapView &start, presentation::ScenePoint moved,
                         float scale) {
  if (!(scale > 0.0F)) {
    return start.at;
  }
  /* The markers' draw takes the ground to the screen by the turn's inverse
     (igMatrix44f::setRotationRadians, row vectors), so the screen goes back
     by the turn. */
  const float c = std::cos(start.turn);
  const float s = std::sin(start.turn);
  const float dx = (moved.x * c - moved.z * s) / scale;
  const float dy = (moved.x * s + moved.z * c) / scale;
  return {held(start.at.x - dx, start.low.x, start.high.x),
          held(start.at.y - dy, start.low.y, start.high.y)};
}

} // namespace x2::input
