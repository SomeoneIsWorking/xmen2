#include "sebas_touch_layout.hpp"

#include <array>

namespace x2::input {
namespace {

/* FUN_0061dc10's boxes and their touch places. */
constexpr std::array<SebasPlace, 14> kMoves{{
    /* FSAA, clear of the finger band round the Resolution slider. */
    {{33, 146, 196, 12}, {33, 166, 196, kSebasButton}},
    /* texs\tabimg.png, the tabs' bar, and Player 1..4. */
    {{269, 33, 408, 44}, {269, 20, 408, 68}},
    {{281, 50, 77, 16}, {281, 30, 77, kSebasButton}},
    {{363, 50, 77, 16}, {363, 30, 77, kSebasButton}},
    {{445, 50, 77, 16}, {445, 30, 77, kSebasButton}},
    {{527, 50, 77, 16}, {527, 30, 77, kSebasButton}},
    /* The binding table's panel, below the taller tabs. */
    {{244, 74, 433, 250}, {244, 88, 433, 236}},
    /* texs\tabimg2.png, the Defaults bar, and Defaults 1..3. */
    {{270, 327, 408, 44}, {270, 324, 408, 70}},
    {{291, 340, 85, 25}, {291, 335, 85, kSebasButton}},
    {{394, 340, 85, 25}, {394, 335, 85, kSebasButton}},
    {{497, 340, 85, 25}, {497, 335, 85, kSebasButton}},
    /* The footer strip's Esc Back and Revert to default. */
    {{80, 428, 80, 30}, {30, 421, 170, kSebasButton}, "Back"},
    {{378, 428, 150, 30}, {360, 421, 210, kSebasButton}},
    /* texs\helpb.png, the footer strip, a little taller round them. */
    {{-45, 415, 721, 60}, {-45, 412, 721, 66}},
}};

} // namespace

std::optional<SebasPlace> sebas_touch_place(SebasBox retail) {
  for (const SebasPlace &place : kMoves) {
    if (place.retail == retail) {
      return place;
    }
  }
  return std::nullopt;
}

std::optional<int> sebas_slider_y(int top, int bottom, int y, int finger) {
  const int centre = (top + bottom) / 2;
  const int reach = finger / 2;
  if (y < centre - reach || y > centre + reach) {
    return std::nullopt;
  }
  return centre;
}

std::vector<SebasBox> sebas_dialog_boxes(
    const std::vector<std::pair<SebasDialogRole, SebasBox>> &widgets) {
  std::optional<SebasBox> first_row;
  int rows = 0;
  for (const auto &[role, box] : widgets) {
    if (role == SebasDialogRole::row) {
      first_row = first_row.value_or(box);
      ++rows;
    }
  }
  if (!first_row) {
    std::vector<SebasBox> boxes;
    for (const auto &widget : widgets) {
      boxes.push_back(widget.second);
    }
    return boxes;
  }
  const int width = (first_row->width - (rows - 1) * kSebasRowGap) / rows;
  std::vector<SebasBox> boxes;
  int next_x = first_row->x;
  for (const auto &[role, box] : widgets) {
    SebasBox placed = box;
    if (role == SebasDialogRole::row) {
      placed = {next_x, first_row->y, width, kSebasButton};
      next_x += width + kSebasRowGap;
    } else if (role == SebasDialogRole::highlight) {
      placed = {first_row->x, first_row->y, width, kSebasButton};
    }
    boxes.push_back(placed);
  }
  return boxes;
}

int sebas_table_notches(int travel, int row) {
  if (row <= 0) {
    return 0;
  }
  return travel / row;
}

} // namespace x2::input
