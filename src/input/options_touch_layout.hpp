#ifndef X2_OPTIONS_TOUCH_LAYOUT_HPP
#define X2_OPTIONS_TOUCH_LAYOUT_HPP

#include "touch_cards.hpp"

/* CMenuOptions on touch: its settings as a column of finger-tall cards
   between the title bar and the Accept bar, scrolled by a drag, a volume
   card holding its bar (docs/touch-play.md). Scene units, z up. */
namespace x2::input {

struct OptionsTouchInput {
  /* The middles of the title's and Accept's text, and the bars' left and
     right. */
  float title_z = 0.0F;
  float accept_z = 0.0F;
  float left = 0.0F;
  float right = 0.0F;
  float finger = 0.0F;
  int rows = 0;
  int first = 0;
};

/* The column clear of the title's and Accept's finger areas, and its
   cards. */
CardStack layout_options_touch(const OptionsTouchInput &input);

/* A volume card's label line (its upper part) and its bar of `height`
   (along its lower part), inset `pad` from the card's ends. */
SceneBox volume_label(const SceneBox &card, float pad);
SceneBox volume_bar(const SceneBox &card, float pad, float height);

/* The share of `bar` from its left at `x`, 0 to 1. */
float bar_share(const SceneBox &bar, float x);

} // namespace x2::input

#endif
