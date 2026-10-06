#ifndef X2_TOUCH_MENU_HPP
#define X2_TOUCH_MENU_HPP

#include "touch_controls.h"
#include "touch_menu_layout.hpp"
#include "touch_menu_view.hpp"

#include <lucent/touch.h>

#include <cstdint>
#include <mutex>
#include <optional>
#include <vector>

namespace x2::input {

/* What the touch menu hands the game: the retail GUI's own mouse click at a
   client point, or one tap of a menu pad button. */
struct TouchMenuDelivery {
  enum class Kind : std::uint8_t { click, pad };
  Kind kind = Kind::click;
  presentation::ClientPoint at;
  TouchAction button = TouchAction::MenuA;
};

/* The touch menu as it is drawn right now, copied for other threads. */
struct TouchMenuState {
  bool shown = false;
  X2LayoutViewport viewport{};
  TouchMenuView view;
  TouchMenuLayout layout;
  /* The button a finger is on, as an index into layout.buttons, or -1. */
  int pressed = -1;
};

/*
 * The port's own touch menu over a retail menu it replaces.
 *
 * Holds the latest view of the retail menu, lays it out, owns the contacts on
 * it (every contact while it is shown: it covers the retail screen), scrolls,
 * and turns a tap into the game's own input. A row with a command is clicked
 * where the game hit-tests it; a row without one, and a left/right step, are
 * reached by walking the game's focus with the menu pad's Up/Down, confirmed
 * on each new view, and then pressed with A, Left or Right; a list entry the
 * game's window does not show is walked to and only selected. A footer action
 * is clicked on the desctext item that carries its token.
 */
class TouchMenu {
public:
  /* The latest read; nullopt when no menu this replaces is up. Returns the
     input a pending focus walk owes now. */
  std::vector<TouchMenuDelivery> set_view(std::optional<TouchMenuView> view,
                                          std::uint64_t now_ms);
  void set_viewport(const X2LayoutViewport &viewport);
  bool shown() const { return view_.has_value() && has_viewport_; }

  /* A contact in output pixels. Only the first finger down acts. */
  std::vector<TouchMenuDelivery> contact(std::int64_t id,
                                         lucent::touch::Point at,
                                         lucent::touch::Phase phase,
                                         std::uint64_t now_ms);
  /* Forget the finger and any focus walk. */
  void cancel();

  const TouchMenuLayout &layout() const { return layout_; }
  TouchMenuState state() const;

private:
  struct Finger {
    std::int64_t id = 0;
    lucent::touch::Point origin;
    float scroll_origin = 0.0F;
    std::optional<std::size_t> button;
    bool dragging = false;
  };
  struct Walk {
    std::uint32_t address = 0;
    unsigned slot = 0;
    int entry = -1;
    std::optional<TouchAction> final_button;
    std::uint64_t started_ms = 0;
    std::uint64_t pressed_ms = 0;
    int focus_at_press = -2;
    bool pressed = false;
  };

  std::vector<TouchMenuDelivery> activate(const TouchMenuButton &button,
                                          std::uint64_t now_ms);
  std::vector<TouchMenuDelivery> advance_walk(std::uint64_t now_ms);
  void relayout();
  void publish();

  std::optional<TouchMenuView> view_;
  X2LayoutViewport viewport_{};
  bool has_viewport_ = false;
  TouchMenuLayout layout_;
  float scroll_ = 0.0F;
  int followed_focus_ = -1;
  std::optional<Finger> finger_;
  std::optional<Walk> walk_;

  mutable std::mutex published_lock_;
  TouchMenuState published_;
};

} // namespace x2::input

#endif
