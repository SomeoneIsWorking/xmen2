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
   client point, one tap of a menu pad button, or a menu pad button put down
   or let go of. */
struct TouchMenuDelivery {
  enum class Kind : std::uint8_t { click, pad, press, release };
  Kind kind = Kind::click;
  presentation::ClientPoint at;
  TouchAction button = TouchAction::MenuA;
};

/* The touch menu as it is drawn right now, copied for other threads. */
struct TouchMenuState {
  bool shown = false;
  x2::presentation::LayoutViewport viewport{};
  TouchMenuView view;
  TouchMenuLayout layout;
  /* The button a finger is on, as an index into layout.buttons, or -1. */
  int pressed = -1;
  /* The held footer whose button is down, as an index into view.footers, or
     -1. */
  int held_footer = -1;
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
  void set_viewport(const x2::presentation::LayoutViewport &viewport);
  bool shown() const { return view_.has_value() && has_viewport_; }

  /* A contact in output pixels. Only the first finger down acts. */
  std::vector<TouchMenuDelivery> contact(std::int64_t id,
                                         lucent::touch::Point at,
                                         lucent::touch::Phase phase,
                                         std::uint64_t now_ms);
  /* Forget the finger and any focus walk; returns the release a held
     button owes. */
  std::vector<TouchMenuDelivery> cancel();

  const TouchMenuLayout &layout() const { return layout_; }
  TouchMenuState state() const;

private:
  struct Finger {
    std::int64_t id = 0;
    lucent::touch::Point origin;
    float scroll_origin = 0.0F;
    std::optional<std::size_t> button;
    bool dragging = false;
    /* Began on detail lines that scroll on their own: drags scroll them. */
    bool on_detail = false;
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
  /* Lets go of the held button, if any, into `out`. */
  void release_held(std::vector<TouchMenuDelivery> *out);
  void relayout();
  void publish();

  std::optional<TouchMenuView> view_;
  x2::presentation::LayoutViewport viewport_{};
  bool has_viewport_ = false;
  TouchMenuLayout layout_;
  float scroll_ = 0.0F;
  float detail_scroll_ = 0.0F;
  int followed_focus_ = -1;
  std::optional<Finger> finger_;
  std::optional<Walk> walk_;
  /* A held footer's button and the menu it was put down on. */
  std::optional<TouchAction> held_;
  std::uint32_t held_address_ = 0;

  mutable std::mutex published_lock_;
  TouchMenuState published_;
};

} // namespace x2::input

#endif
