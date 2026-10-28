#ifndef X2_TOUCH_TARGETS_HPP
#define X2_TOUCH_TARGETS_HPP

#include "touch_cards.hpp"

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

/* Finger-sized buttons drawn over a menu item too small to touch: each draw
   puts the button's area and the point in the item the game hit-tests, and a
   press that starts in the area is the game's pointer at that point until it
   lifts (docs/touch-play.md). */
namespace x2::input {

/* What a press on a target hands the game. */
enum class TouchPress : std::uint8_t {
  /* The left button, down and up at the aim. */
  click,
  /* Nothing: the port acts on the tap itself (take_tap). */
  own,
};

struct TouchTarget {
  SceneBox area;
  presentation::ScenePoint aim;
  TouchPress press = TouchPress::click;
  /* What a port-drawn button does, for GET /menu; empty for an item's. */
  std::string_view name;
  /* Where a port-drawn button's name and value letters land, for GET
     /menu. */
  std::optional<SceneBox> name_text;
  std::optional<SceneBox> value_text;
};

class TouchTargets {
public:
  /* A button drawn this frame. */
  void put(const TouchTarget &target);
  /* This frame's buttons become the shown ones; the pointer reads those,
     and a tap the last frame's draws did not take is dropped. */
  void next_frame();

  /* A press at `point`: the shown button it starts in, the one whose middle
     is nearest where buttons overlap, held until release(). */
  std::optional<TouchTarget> press(presentation::ScenePoint point);
  /* The lift at `point`; an own button's press that lifts inside it is a
     tap. */
  void release(presentation::ScenePoint point);
  /* True while a press holds the button over `area`. */
  bool pressed(const SceneBox &area) const;
  /* True while a press holds any button. */
  bool holding() const { return held_.has_value(); }
  /* True once for a tap on the own button over `area`, in the draws of
     the frame it lifted in. */
  bool take_tap(const SceneBox &area);

  /* The pointer passes over `aim` once, no button held, as a mouse does:
     CMenuTeam's onMouse steps its focus one row toward the row under the
     pointer per message (0x005e25c0). */
  void hover(presentation::ScenePoint aim);
  std::optional<presentation::ScenePoint> take_hover();

  const std::vector<TouchTarget> &shown() const { return shown_; }

private:
  std::vector<TouchTarget> drawing_;
  std::vector<TouchTarget> shown_;
  std::optional<TouchTarget> held_;
  std::optional<SceneBox> tapped_;
  std::optional<presentation::ScenePoint> hover_;
};

/* Written by the menu draws and read by the pointer, both on the guest
   thread. */
TouchTargets &touch_targets();

} // namespace x2::input

#endif
