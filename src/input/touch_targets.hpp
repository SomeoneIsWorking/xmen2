#ifndef X2_TOUCH_TARGETS_HPP
#define X2_TOUCH_TARGETS_HPP

#include "touch_cards.hpp"

#include <optional>
#include <vector>

/* Finger-sized buttons drawn over a menu item too small to touch: each draw
   puts the button's area and the point in the item the game hit-tests, and a
   press that starts in the area is the game's pointer at that point until it
   lifts (docs/touch-play.md). */
namespace x2::input {

struct TouchTarget {
  SceneBox area;
  presentation::ScenePoint aim;
};

class TouchTargets {
public:
  /* A button drawn this frame. */
  void put(const TouchTarget &target);
  /* This frame's buttons become the shown ones; the pointer reads those. */
  void next_frame();

  /* A press at `point`: the aim of the shown button it starts in, held
     until release(). */
  std::optional<presentation::ScenePoint> press(presentation::ScenePoint point);
  void release();
  /* True while a press holds the button over `area`. */
  bool pressed(const SceneBox &area) const;

  const std::vector<TouchTarget> &shown() const { return shown_; }

private:
  std::vector<TouchTarget> drawing_;
  std::vector<TouchTarget> shown_;
  std::optional<SceneBox> held_;
};

/* Written by the menu draws and read by the pointer, both on the guest
   thread. */
TouchTargets &touch_targets();

} // namespace x2::input

#endif
