#ifndef X2_TEXT_TOUCH_LAYOUT_HPP
#define X2_TEXT_TOUCH_LAYOUT_HPP

#include "touch_cards.hpp"

/* A small text item on touch, a `$MENU_*` prompt, a shop tab or a menu row:
   the finger-tall button over its text (docs/touch-play.md). Scene units, z
   up. */
namespace x2::input {

/* Button width past the item's text, in fingers. */
inline constexpr float kTextButtonGrow = 0.25F;

/* The button over an item whose text is drawn in `text`, for a finger of
   `finger`. */
SceneBox text_button(const SceneBox &text, float finger);

/* The button over a row whose text is drawn in `text` on a bar from `left`
   to `right`: the bar's width, a finger tall about the text. */
SceneBox row_button(const SceneBox &text, float left, float right,
                    float finger);

} // namespace x2::input

#endif
