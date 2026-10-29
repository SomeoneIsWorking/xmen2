#include "page_touch_layout.hpp"

#include <cmath>

namespace x2::input {

PageGesture page_gesture(presentation::ScenePoint from,
                         presentation::ScenePoint to, float finger) {
  const float across = to.x - from.x;
  const float up = to.z - from.z;
  if (std::hypot(across, up) < kTapTravel * finger) {
    return PageGesture::tap;
  }
  if (std::fabs(across) < kSwipeTravel * finger ||
      std::fabs(across) < std::fabs(up)) {
    return PageGesture::none;
  }
  return across < 0.0F ? PageGesture::swipe_next : PageGesture::swipe_prev;
}

} // namespace x2::input
