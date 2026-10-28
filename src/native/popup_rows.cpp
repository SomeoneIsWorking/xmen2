#include "popup_rows.hpp"

#include "../input/menu_touch_gaps.hpp"

#include <algorithm>

namespace x2::native {
namespace {

PopupRowsDrawn drawn;

} // namespace

float popup_row_slot(float line, const presentation::RetailScenePlane &plane) {
  const float finger = input::menu_finger_target(plane) * plane.height() /
                       static_cast<float>(plane.client_height());
  return std::max(line, finger);
}

PopupRowsDrawn &popup_rows_drawn() { return drawn; }

} // namespace x2::native
