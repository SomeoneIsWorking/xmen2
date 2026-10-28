#ifndef X2_OPTIONS_TOUCH_HPP
#define X2_OPTIONS_TOUCH_HPP

#include "../input/touch_cards.hpp"
#include "x86rt.h"

#include <cstdint>
#include <optional>
#include <string_view>

/* CMenuOptions on touch (docs/touch-play.md): its settings drawn as a
   column of finger cards of the game's option art, scrolled by a drag; the
   text items' draw (text_item_touch) hands its rows here. */
namespace x2::native {

/* The touch draw of a CMenuOptions text item `name`: true when touch drew
   it (a setting's label draws its card, its value is drawn on that card),
   false to leave it to retail's draw. */
bool options_touch_draw(const CPU &cpu, std::uint32_t self,
                        std::string_view menu_class, std::string_view name);
/* Retail draws `self` again: its card and the hidden panel come back. */
void options_touch_restore(const CPU &cpu, std::uint32_t self);
/* A text item's destructor: a label's card goes with it. */
void options_touch_destroy(const CPU &cpu, std::uint32_t self);

/* Where the last touch draw put a setting's card, and its volume bar. */
struct OptionsTouchCard {
  input::SceneBox card;
  std::optional<input::SceneBox> bar;
};
std::optional<OptionsTouchCard> options_touch_card(std::uint32_t label);

} // namespace x2::native

#endif
