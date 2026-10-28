#ifndef X2_TOUCH_CARDS_HPP
#define X2_TOUCH_CARDS_HPP

#include "../presentation/retail_scene_plane.hpp"

#include <vector>

/* A column of finger-tall cards that a drag scrolls: the entries of a touch
   popup or list box (docs/touch-play.md). Scene units, z up. */
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

/* Card height, the gap between cards and card text height, in fingers. */
inline constexpr float kCardHeight = 1.0F;
inline constexpr float kCardGap = 0.15F;
inline constexpr float kCardText = 0.42F;

struct CardStack {
  /* Cards that fit; `cards` holds the shown ones from `first`. */
  int rows = 0;
  int entries = 0;
  int first = 0;
  float pitch = 0.0F;
  std::vector<SceneBox> cards;
  bool more_above = false;
  bool more_below = false;
};

/* The cards a column `height` tall holds, at least one. */
int card_rows(float height, float finger, int max_rows);

/* Cards from `area`'s top, `first` clamped so the last card is filled. */
CardStack stack_cards(const SceneBox &area, float finger, int entries,
                      int first, int max_rows);

/* The first entry a drag from `start_first` by `dz` scene units shows. */
int cards_scrolled(const CardStack &stack, int start_first, float dz);

/* The index of the box holding `point`, or -1. */
int box_hit(const std::vector<SceneBox> &boxes, presentation::ScenePoint point);

} // namespace x2::input

#endif
