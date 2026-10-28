#ifndef X2_LIST_TOUCH_HPP
#define X2_LIST_TOUCH_HPP

#include "../input/touch_cards.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace x2::native {

/* What the last touch draw of a list box showed: its cards from entry
   `first`. */
struct ListTouchDrawn {
  int first = 0;
  std::vector<input::SceneBox> cards;
};

/* The list box `list` as touch last drew it, or nullopt; guest thread. */
std::optional<ListTouchDrawn> list_touch_drawn(std::uint32_t list);

/* The list box of `menu` drawn on touch that a pointer message at `point`
   belongs to: the one a finger holds, else the one whose cards are under
   it; 0 for none. Guest thread. */
std::uint32_t list_touch_owner(std::uint32_t menu,
                               presentation::ScenePoint point);

} // namespace x2::native

#endif
