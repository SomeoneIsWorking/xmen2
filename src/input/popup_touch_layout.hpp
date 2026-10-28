#ifndef X2_POPUP_TOUCH_LAYOUT_HPP
#define X2_POPUP_TOUCH_LAYOUT_HPP

#include "../presentation/retail_scene_plane.hpp"

#include <vector>

/* CPopupDialog on touch: the frame grown to most of the screen, the entries as
   finger-tall cards that scroll, the footer actions as buttons along the
   bottom. Scene units, z up (docs/touch-play.md). */
namespace x2::input {

struct SceneBox {
  float left = 0.0F;
  float bottom = 0.0F;
  float right = 0.0F;
  float top = 0.0F;

  float width() const { return right - left; }
  float height() const { return top - bottom; }
  bool contains(presentation::ScenePoint point) const {
    return point.x >= left && point.x < right && point.z >= bottom &&
           point.z < top;
  }
};

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
  /* Cards that fit; `cards` holds the shown ones from `first_shown`. */
  int rows = 0;
  int first_shown = 0;
  float pitch = 0.0F;
  std::vector<SceneBox> cards;
  std::vector<SceneBox> buttons;
  /* Where the scroll arrows go when entries lie above or below. */
  bool more_above = false;
  bool more_below = false;
  presentation::ScenePoint arrow_up;
  presentation::ScenePoint arrow_down;
};

PopupTouchLayout layout_popup_touch(const PopupTouchInput &input);

/* The shown row a drag from `start_first` by `dz` scene units scrolls to. */
int popup_touch_scrolled(const PopupTouchLayout &layout, int entries,
                         int start_first, float dz);

/* The index of the box holding `point`, or -1. */
int popup_touch_hit(const std::vector<SceneBox> &boxes,
                    presentation::ScenePoint point);

} // namespace x2::input

#endif
