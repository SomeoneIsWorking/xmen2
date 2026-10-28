#include "team_touch_layout.hpp"

#include <algorithm>

namespace x2::input {
namespace {

float hero_row_bottom(float finger) {
  return kTeamColumn.top - kCardHeight * finger;
}

} // namespace

std::array<SceneBox, 2> team_hero_buttons(float finger) {
  return team_row_halves({kTeamColumn.left, hero_row_bottom(finger),
                          kTeamColumn.right, kTeamColumn.top},
                         finger);
}

std::array<SceneBox, 2> team_row_halves(const SceneBox &row, float finger) {
  const float gap = kTeamButtonGap * finger;
  const float middle = 0.5F * (row.left + row.right);
  return {SceneBox{row.left, row.bottom, middle - 0.5F * gap, row.top},
          SceneBox{middle + 0.5F * gap, row.bottom, row.right, row.top}};
}

std::vector<SceneBox> team_rows(int rows, float finger) {
  std::vector<SceneBox> out;
  if (rows <= 0) {
    return out;
  }
  const float top = hero_row_bottom(finger) - kCardGap * finger;
  const float space = top - kTeamColumn.bottom;
  float height =
      std::min(kCardHeight * finger, space / static_cast<float>(rows));
  float gap = 0.0F;
  if (rows > 1) {
    gap = std::clamp((space - static_cast<float>(rows) * height) /
                         static_cast<float>(rows - 1),
                     0.0F, kCardGap * finger);
  }
  for (int i = 0; i < rows; ++i) {
    const float row_top = top - static_cast<float>(i) * (height + gap);
    out.push_back(
        {kTeamColumn.left, row_top - height, kTeamColumn.right, row_top});
  }
  return out;
}

TeamSlotButtons team_slot_buttons(float finger) {
  const std::vector<SceneBox> rows = team_rows(3, finger);
  const std::array<SceneBox, 2> sides = team_row_halves(rows[0], finger);
  return {sides[0], sides[1], rows[1], rows[2]};
}

TeamStatRow team_stat_row(const SceneBox &row, float finger) {
  const float gap = kTeamButtonGap * finger;
  const float side = std::min(finger, row.height());
  const SceneBox add{row.right - side, row.bottom, row.right, row.top};
  const SceneBox remove{add.left - gap - side, row.bottom, add.left - gap,
                        row.top};
  return {{row.left, row.bottom, remove.left - gap, row.top}, remove, add};
}

} // namespace x2::input
