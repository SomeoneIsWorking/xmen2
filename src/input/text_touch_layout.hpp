#ifndef X2_TEXT_TOUCH_LAYOUT_HPP
#define X2_TEXT_TOUCH_LAYOUT_HPP

#include "touch_cards.hpp"

/* A small text item on touch, a `$MENU_*` prompt or a shop tab: the
   finger-tall button over its text (docs/touch-play.md). Scene units, z
   up. */
namespace x2::input {

/* Button width past the item's text, in fingers. */
inline constexpr float kTextButtonGrow = 0.25F;

/* The button over an item whose text is drawn in `text`, for a finger of
   `finger`. */
SceneBox text_button(const SceneBox &text, float finger);

} // namespace x2::input

#endif
