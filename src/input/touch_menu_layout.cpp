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
constexpr float kTabHeight = 64.0F;
/* The detail band: padding, one line of facts and three of text. */
constexpr float kDetailHeight = 176.0F;
constexpr float kDetailPad = 13.0F;
constexpr float kFactsHeight = 44.0F;
constexpr float kDetailLineHeight = 32.0F;
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
  case TouchMenuPart::tab:
    return "tab";
  }
  return "row";
}

std::optional<std::size_t> TouchMenuLayout::hit(float x, float y) const {
  for (std::size_t i = 0; i < buttons.size(); ++i) {
    const TouchMenuButton &button = buttons[i];
    const bool pinned = button.part == TouchMenuPart::footer ||
                        button.part == TouchMenuPart::tab;
    if (!pinned && !inside(list, x, y)) {
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
                                  float scroll, float detail_scroll) {
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
  const float tabs_top = layout.title.bottom + (titled ? kRowGap * u : 0.0F);
  const float tab_height =
      view.tabs.empty() ? 0.0F : std::max(kTabHeight * u, kMinimumTarget);
  layout.tabs = {column_left, tabs_top, column_right, tabs_top + tab_height};
  const bool detailed = !view.facts.empty() || !view.detail.empty();
  layout.reading = view.rows.empty() && !view.detail.empty();
  const float detail_bottom =
      layout.footer.top - (view.footers.empty() ? 0.0F : kRowGap * u);
  const float body_top =
      layout.tabs.bottom + (view.tabs.empty() ? 0.0F : kRowGap * u);
  const float band = layout.reading ? detail_bottom - body_top
                     : detailed     ? kDetailHeight * u
                                    : 0.0F;
  layout.detail = {column_left, detail_bottom - band, column_right,
                   detail_bottom};
  layout.list = {column_left, body_top, column_right,
                 layout.reading
                     ? detail_bottom
                     : layout.detail.top - (detailed ? kRowGap * u : 0.0F)};
  layout.detail_line_height = kDetailLineHeight * u;
  layout.detail_text_top = layout.detail.top + kDetailPad * u +
                           (view.facts.empty() ? 0.0F : kFactsHeight * u);
  layout.detail_text = layout.detail;
  if (!layout.reading) {
    layout.detail_text.top = layout.detail_text_top;
    const float lines =
        static_cast<float>(view.detail.size()) * layout.detail_line_height;
    layout.detail_max_scroll = std::max(
        lines - (layout.detail_text.bottom - layout.detail_text.top), 0.0F);
    layout.detail_scroll =
        std::clamp(std::isfinite(detail_scroll) ? detail_scroll : 0.0F, 0.0F,
                   layout.detail_max_scroll);
    layout.detail_text_top -= layout.detail_scroll;
  }

  const float row_height = std::max(kRowHeight * u, kMinimumTarget);
  const float gap = kRowGap * u;
  /* Read text scrolls in the list's place; rows scroll in the list. */
  const float content =
      layout.reading
          ? static_cast<float>(view.detail.size()) * layout.detail_line_height +
                2.0F * kDetailPad * u
          : static_cast<float>(view.rows.size()) * (row_height + gap) - gap;
  layout.max_scroll =
      std::max(content - (layout.list.bottom - layout.list.top), 0.0F);
  layout.scroll = std::clamp(std::isfinite(scroll) ? scroll : 0.0F, 0.0F,
                             layout.max_scroll);
  if (layout.reading) {
    layout.detail_text_top -= layout.scroll;
  }

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

  const std::size_t tabs = view.tabs.size();
  if (tabs > 0u) {
    const float each = column / static_cast<float>(tabs);
    for (std::size_t i = 0; i < tabs; ++i) {
      const float x = column_left + each * static_cast<float>(i);
      layout.buttons.push_back(
          {TouchMenuPart::tab,
           static_cast<int>(i),
           {x, layout.tabs.top, x + each, layout.tabs.bottom}});
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
