#ifndef X2_TOUCH_MENU_LAYOUT_HPP
#define X2_TOUCH_MENU_LAYOUT_HPP

#include "../presentation/touch_layout.h"
#include "touch_menu_view.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace x2::input {

enum class TouchMenuPart : std::uint8_t {
  row,
  step_left,
  step_right,
  footer,
  tab
};

const char *touch_menu_part_name(TouchMenuPart part);

struct TouchMenuButton {
  TouchMenuPart part = TouchMenuPart::row;
  /* Index into the view's rows, its footers for a footer, its tabs for a
     tab. */
  int index = 0;
  /* Output pixels, scroll applied; a row may extend past the list. */
  x2::presentation::Rect rect{};
};

/* Where everything of the touch menu is, in output pixels. The document draws
   exactly this and contacts are tested against exactly this. */
struct TouchMenuLayout {
  /* Output pixels per design unit; the document scales its type by it. */
  float unit = 1.0F;
  x2::presentation::Rect title{};
  /* The tab bar, pinned above the list; empty without tabs. */
  x2::presentation::Rect tabs{};
  /* The scrolling area the rows live in. */
  x2::presentation::Rect list{};
  /* The facts and detail text, pinned below the list; empty without them.
     When reading (a view with no rows) it is the list's own place. */
  x2::presentation::Rect detail{};
  bool reading = false;
  /* Where the first detail line's top is, its scroll applied, and each
     line's height; lines outside `detail_text` are not drawn. */
  float detail_text_top = 0.0F;
  float detail_line_height = 0.0F;
  /* The part of `detail` its lines may occupy: below the facts. */
  x2::presentation::Rect detail_text{};
  /* Beside a list the detail lines scroll on their own; read text scrolls
     with `scroll`. */
  float detail_scroll = 0.0F;
  float detail_max_scroll = 0.0F;
  x2::presentation::Rect footer{};
  std::vector<TouchMenuButton> buttons;
  float scroll = 0.0F;
  float max_scroll = 0.0F;

  /* The button under a point; a row only where it is inside the list. A tab
     or footer is never scrolled. */
  std::optional<std::size_t> hit(float x, float y) const;
};

/* Lays the view out inside the viewport's safe area. `scroll` is clamped to
   what the rows need, `detail_scroll` to what the detail lines beside them
   need. */
TouchMenuLayout
layout_touch_menu(const TouchMenuView &view,
                  const x2::presentation::LayoutViewport &viewport,
                  float scroll, float detail_scroll);

/* The scroll that brings row `row` fully into the list, from `scroll`. */
float touch_menu_scroll_to(const TouchMenuLayout &layout, int row);

} // namespace x2::input

#endif
