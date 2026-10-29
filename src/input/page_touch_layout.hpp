#ifndef X2_PAGE_TOUCH_LAYOUT_HPP
#define X2_PAGE_TOUCH_LAYOUT_HPP

#include "../presentation/retail_scene_plane.hpp"

/* A finger's gestures on the retail pages with no list of their own: the
   image viewer and the credits (docs/touch-play.md). Scene units, z up. */
namespace x2::input {

/* Travel along x past which a lift is a swipe, in fingers. */
inline constexpr float kSwipeTravel = 1.0F;
/* Travel under which a lift is a tap, in fingers. */
inline constexpr float kTapTravel = 0.5F;

enum class PageGesture : int {
  none,
  tap,
  /* Toward the left: the next page. */
  swipe_next,
  /* Toward the right: the previous page. */
  swipe_prev,
};

/* What a finger pressed at `from` and lifted at `to` did, for a finger of
   `finger`: a swipe is mostly across and at least kSwipeTravel long. */
PageGesture page_gesture(presentation::ScenePoint from,
                         presentation::ScenePoint to, float finger);

} // namespace x2::input

#endif
