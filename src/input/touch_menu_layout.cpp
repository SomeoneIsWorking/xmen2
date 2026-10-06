#include "touch_menu_layout.hpp"

#include <algorithm>
#include <cmath>

namespace x2::input {
namespace {

/* Design units: a 720-unit short edge. */
constexpr float kDesignShortEdge = 720.0F;
constexpr float kMinimumUnit = 0.75F;
constexpr float kMargin = 24.0F;
constexpr float kTitleHeight = 72.0F;
constexpr float kFooterHeight = 96.0F;
constexpr float kFooterButtonHeight = 68.0F;
constexpr float kFooterButtonWidth = 300.0F;
constexpr float kRowHeight = 72.0F;
constexpr float kRowGap = 12.0F;
constexpr float kStepWidth = 96.0F;
constexpr float kStepGap = 10.0F;
constexpr float kListWidth = 960.0F;
/* The gameplay controls' own minimum target, in output pixels. */
constexpr float kMinimumTarget = 48.0F;

bool inside(const X2Rect &rect, float x, float y) {
  return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

} // namespace

const char *touch_menu_part_name(TouchMenuPart part) {
  switch (part) {
  case TouchMenuPart::row:
    return "row";
  case TouchMenuPart::step_left:
    return "step-left";
  case TouchMenuPart::step_right:
    return "step-right";
  case TouchMenuPart::footer:
    return "footer";
  }
  return "row";
}

std::optional<std::size_t> TouchMenuLayout::hit(float x, float y) const {
  for (std::size_t i = 0; i < buttons.size(); ++i) {
    const TouchMenuButton &button = buttons[i];
    if (button.part != TouchMenuPart::footer && !inside(list, x, y)) {
      continue;
    }
    if (inside(button.rect, x, y)) {
      return i;
    }
  }
  return std::nullopt;
}

TouchMenuLayout layout_touch_menu(const TouchMenuView &view,
                                  const X2LayoutViewport &viewport,
                                  float scroll) {
  TouchMenuLayout layout;
  const float left = viewport.safe_left;
  const float top = viewport.safe_top;
  const float right = viewport.width - viewport.safe_right;
  const float bottom = viewport.height - viewport.safe_bottom;
  const float width = std::max(right - left, 1.0F);
  const float height = std::max(bottom - top, 1.0F);
  const float u =
      std::max(std::min(width, height) / kDesignShortEdge, kMinimumUnit);
  layout.unit = u;

  const float margin = kMargin * u;
  const float column = std::min(width - 2.0F * margin, kListWidth * u);
  const float column_left = left + 0.5F * (width - column);
  const float column_right = column_left + column;

  /* A menu with no title item gets no header band. */
  const bool titled = !view.title.empty();
  layout.title = {column_left, top + margin, column_right,
                  top + margin + (titled ? kTitleHeight * u : 0.0F)};
  const float footer_height = view.footers.empty() ? 0.0F : kFooterHeight * u;
  layout.footer = {column_left, bottom - margin - footer_height, column_right,
                   bottom - margin};
  layout.list = {
      column_left, layout.title.bottom + (titled ? kRowGap * u : 0.0F),
      column_right,
      layout.footer.top - (view.footers.empty() ? 0.0F : kRowGap * u)};

  const float row_height = std::max(kRowHeight * u, kMinimumTarget);
  const float gap = kRowGap * u;
  const float content =
      static_cast<float>(view.rows.size()) * (row_height + gap) - gap;
  layout.max_scroll =
      std::max(content - (layout.list.bottom - layout.list.top), 0.0F);
  layout.scroll = std::clamp(std::isfinite(scroll) ? scroll : 0.0F, 0.0F,
                             layout.max_scroll);

  const float step = std::max(kStepWidth * u, kMinimumTarget);
  const float step_gap = kStepGap * u;
  for (std::size_t i = 0; i < view.rows.size(); ++i) {
    const float row_top = layout.list.top - layout.scroll +
                          static_cast<float>(i) * (row_height + gap);
    const float row_bottom = row_top + row_height;
    const int index = static_cast<int>(i);
    if (view.rows[i].steps) {
      layout.buttons.push_back(
          {TouchMenuPart::step_left,
           index,
           {column_left, row_top, column_left + step, row_bottom}});
      layout.buttons.push_back({TouchMenuPart::row,
                                index,
                                {column_left + step + step_gap, row_top,
                                 column_right - step - step_gap, row_bottom}});
      layout.buttons.push_back(
          {TouchMenuPart::step_right,
           index,
           {column_right - step, row_top, column_right, row_bottom}});
    } else {
      layout.buttons.push_back(
          {TouchMenuPart::row,
           index,
           {column_left, row_top, column_right, row_bottom}});
    }
  }

  const std::size_t footers = view.footers.size();
  if (footers > 0u) {
    const float footer_gap = kRowGap * u;
    const float each =
        std::min(kFooterButtonWidth * u,
                 (column - footer_gap * static_cast<float>(footers - 1u)) /
                     static_cast<float>(footers));
    const float total = each * static_cast<float>(footers) +
                        footer_gap * static_cast<float>(footers - 1u);
    const float button_height =
        std::max(kFooterButtonHeight * u, kMinimumTarget);
    const float button_top =
        layout.footer.top +
        0.5F * ((layout.footer.bottom - layout.footer.top) - button_height);
    float x = column_left + 0.5F * (column - total);
    for (std::size_t i = 0; i < footers; ++i) {
      layout.buttons.push_back(
          {TouchMenuPart::footer,
           static_cast<int>(i),
           {x, button_top, x + each, button_top + button_height}});
      x += each + footer_gap;
    }
  }
  return layout;
}

float touch_menu_scroll_to(const TouchMenuLayout &layout, int row) {
  for (const TouchMenuButton &button : layout.buttons) {
    if (button.part != TouchMenuPart::row || button.index != row) {
      continue;
    }
    float scroll = layout.scroll;
    if (button.rect.top < layout.list.top) {
      scroll -= layout.list.top - button.rect.top;
    } else if (button.rect.bottom > layout.list.bottom) {
      scroll += button.rect.bottom - layout.list.bottom;
    }
    return std::clamp(scroll, 0.0F, layout.max_scroll);
  }
  return layout.scroll;
}

} // namespace x2::input
