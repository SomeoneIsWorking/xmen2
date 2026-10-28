#ifndef X2_TEAM_TOUCH_LAYOUT_HPP
#define X2_TEAM_TOUCH_LAYOUT_HPP

#include "touch_cards.hpp"

#include <array>
#include <vector>

/* A hero's detail tabs on touch (docs/touch-play.md): the column left of the
   retail panel, where the hero stands, holds the tab's finger-sized controls,
   and its top row turns to the previous and next hero. Scene units, z up. */
namespace x2::input {

/* The column: from inside the left frame to the details panel's bracket
   (x 182), and from above the footer buttons to the top of the tab row. */
inline constexpr SceneBox kTeamColumn{8.0F, 52.0F, 178.0F, 380.4F};
/* The gap between the buttons of one row, in fingers. */
inline constexpr float kTeamButtonGap = 0.1F;

/* The previous and next hero buttons, the column's top row. */
std::array<SceneBox, 2> team_hero_buttons(float finger);

/* `rows` finger-tall cards below the hero row, a card's gap apart or less
   so that they fit, shorter only when not even touching ones would. */
std::vector<SceneBox> team_rows(int rows, float finger);

/* A stat's card: its name and value, then its remove and add buttons, a
   finger square each, at the card's right. */
struct TeamStatRow {
  SceneBox name;
  SceneBox remove;
  SceneBox add;
};
TeamStatRow team_stat_row(const SceneBox &row, float finger);

/* While a skill is being assigned: the buttons for the power diamond's left,
   right and bottom slots, laid out as the diamond holds them (left and right
   side by side, bottom below), then Cancel. */
struct TeamSlotButtons {
  SceneBox left;
  SceneBox right;
  SceneBox bottom;
  SceneBox cancel;
};
TeamSlotButtons team_slot_buttons(float finger);

/* A row split into two buttons side by side. */
std::array<SceneBox, 2> team_row_halves(const SceneBox &row, float finger);

} // namespace x2::input

#endif
