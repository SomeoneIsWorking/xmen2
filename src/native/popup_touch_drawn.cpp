#include "popup_touch_drawn.hpp"

namespace x2::native {
namespace {

PopupTouchDrawn drawn;

} // namespace

PopupTouchDrawn &popup_touch_drawn() { return drawn; }

} // namespace x2::native
