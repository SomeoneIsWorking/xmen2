#ifndef X2_POPUP_TOUCH_DRAWN_HPP
#define X2_POPUP_TOUCH_DRAWN_HPP

#include "../input/popup_touch_layout.hpp"

#include <string>
#include <vector>

namespace x2::native {

/* What the last touch popup draw showed: its frame, the span its text was
   wrapped into, each card from the page's entry `first_shown`, which is list
   entry `first_entry`, and each footer button with its words. */
struct PopupTouchButton {
  input::SceneBox box;
  std::string label;
};

struct PopupTouchDrawn {
  bool shown = false;
  int first_shown = 0;
  int first_entry = 0;
  input::SceneBox frame;
  input::SceneBox text;
  std::vector<input::SceneBox> cards;
  std::vector<PopupTouchButton> buttons;
};

/* Written by the popup draw on the guest thread, read there by GET /menu. */
PopupTouchDrawn &popup_touch_drawn();

} // namespace x2::native

#endif
