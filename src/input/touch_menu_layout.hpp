#ifndef X2_TOUCH_MENU_LAYOUT_HPP
#define X2_TOUCH_MENU_LAYOUT_HPP

#include "../presentation/touch_layout.h"
#include "touch_menu_view.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace x2::input {

enum class TouchMenuPart : std::uint8_t { row, step_left, step_right, footer };

const char *touch_menu_part_name(TouchMenuPart part);

struct TouchMenuButton {
  TouchMenuPart part = TouchMenuPart::row;
  /* Index into the view's rows, or its footers for a footer. */
  int index = 0;
  /* Output pixels, scroll applied; a row may extend past the list. */
  X2Rect rect{};
};

/* Where everything of the touch menu is, in output pixels. The document draws
   exactly this and contacts are tested against exactly this. */
struct TouchMenuLayout {
  /* Output pixels per design unit; the document scales its type by it. */
  float unit = 1.0F;
  X2Rect title{};
  /* The scrolling area the rows live in. */
  X2Rect list{};
  X2Rect footer{};
  std::vector<TouchMenuButton> buttons;
  float scroll = 0.0F;
  float max_scroll = 0.0F;

  /* The button under a point; a row only where it is inside the list. */
  std::optional<std::size_t> hit(float x, float y) const;
};

/* Lays the view out inside the viewport's safe area. `scroll` is clamped to
   what the rows need. */
TouchMenuLayout layout_touch_menu(const TouchMenuView &view,
                                  const X2LayoutViewport &viewport,
                                  float scroll);

/* The scroll that brings row `row` fully into the list, from `scroll`. */
float touch_menu_scroll_to(const TouchMenuLayout &layout, int row);

} // namespace x2::input

#endif
