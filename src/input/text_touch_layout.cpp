#include "text_touch_layout.hpp"

namespace x2::input {

SceneBox text_button(const SceneBox &text, float finger) {
  const float half_width = 0.5F * (text.width() + kTextButtonGrow * finger);
  const float half_height = 0.5F * kCardHeight * finger;
  const float x = 0.5F * (text.left + text.right);
  const float z = 0.5F * (text.bottom + text.top);
  return {x - half_width, z - half_height, x + half_width, z + half_height};
}

} // namespace x2::input
