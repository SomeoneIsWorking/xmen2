#ifndef X2_TEXT_TOUCH_LAYOUT_HPP
#define X2_TEXT_TOUCH_LAYOUT_HPP

#include "touch_cards.hpp"

#include <vector>

/* A small text item on touch, a `$MENU_*` prompt, a shop tab or a menu row:
   the finger-tall button over its text (docs/touch-play.md). Scene units, z
   up. */
namespace x2::input {

/* Button width past the item's text, in fingers. */
inline constexpr float kTextButtonGrow = 0.25F;

/* The button over an item whose text is drawn in `text`, for a finger of
   `finger`. */
SceneBox text_button(const SceneBox &text, float finger);

/* The gap between neighbouring footer buttons, in fingers. */
inline constexpr float kFooterGap = 0.1F;

/* The buttons over a menu's footers, whose texts are drawn in `texts`: each
   one's text_button, unless two of a row (footers whose texts share a
   height) would touch, when that row's buttons split its span into equal
   shares a gap apart, in the texts' order across. */
std::vector<SceneBox> footer_buttons(const std::vector<SceneBox> &texts,
                                     float finger);

/* The button over a row whose text is drawn in `text` on a bar from `left`
   to `right`: the bar's width, a finger tall about the text. */
SceneBox row_button(const SceneBox &text, float left, float right,
                    float finger);

} // namespace x2::input

#endif
