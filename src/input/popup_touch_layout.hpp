#ifndef X2_POPUP_TOUCH_LAYOUT_HPP
#define X2_POPUP_TOUCH_LAYOUT_HPP

#include "touch_cards.hpp"

#include <vector>

/* CPopupDialog on touch: the frame grown to most of the screen, the entries as
   finger-tall cards that scroll, the footer actions as buttons along the
   bottom. Scene units, z up (docs/touch-play.md). */
namespace x2::input {

struct PopupTouchInput {
  presentation::RetailScenePlane plane;
  /* The frame model's authored extents, at its retail placement. */
  SceneBox frame;
  /* Where retail puts the text box's left and top inside that frame. */
  float text_left = 0.0F;
  float text_top = 0.0F;
  /* One line of the popup's font at scale 1, and a finger. */
  float line = 0.0F;
  float finger = 0.0F;
  int entries = 0;
  int first_shown = 0;
  int footers = 0;
  /* The most cards the caller can draw. */
  int max_rows = 1;
};

struct PopupTouchLayout {
  /* The frame model's transform: scale across and up, then this
     translation. */
  presentation::ScenePoint frame_scale{1.0F, 1.0F};
  presentation::ScenePoint frame_offset;
  SceneBox frame;
  /* Text is placed by its left end's middle. */
  presentation::ScenePoint title;
  float title_scale = 1.0F;
  float text_scale = 1.0F;
  CardStack cards;
  std::vector<SceneBox> buttons;
  /* Where the scroll arrows go when entries lie above or below. */
  presentation::ScenePoint arrow_up;
  presentation::ScenePoint arrow_down;
};

PopupTouchLayout layout_popup_touch(const PopupTouchInput &input);

} // namespace x2::input

#endif
