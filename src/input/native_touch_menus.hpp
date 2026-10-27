#ifndef X2_NATIVE_TOUCH_MENUS_HPP
#define X2_NATIVE_TOUCH_MENUS_HPP

#include <string_view>

namespace x2::input {

/* The retail menu classes a finger drives directly, as the game's own pointer,
   with nothing drawn over them (docs/touch-play.md). */
bool menu_is_native_on_touch(std::string_view menu_class);

} // namespace x2::input

#endif
