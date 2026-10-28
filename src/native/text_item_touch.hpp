#ifndef X2_TEXT_ITEM_TOUCH_HPP
#define X2_TEXT_ITEM_TOUCH_HPP

#include "../presentation/retail_scene_plane.hpp"

#include <cstdint>
#include <string_view>

namespace x2::native {

/* The text items touch draws as a finger button: a `$MENU_*` prompt in a
   menu that is native on touch. */
bool text_has_touch_button(std::string_view menu_class, std::string_view text);

/* The text items touch does not draw: a `$DPAD_*` hint (`Down Scroll`) in a
   menu native on touch, where a drag does what the pad would. */
bool text_is_pad_hint(std::string_view menu_class, std::string_view text);

/* The text items touch gives a finger target and leaves retail's draw: the
   tabs of the shop, stash, review, danger room and world map, the world
   map's point rows, and the rows of the main menu and the PDA. */
bool text_has_finger_area(std::string_view menu_class, std::string_view name);

/* The middle of the raw box a menu's onMouse tests a text item with. */
presentation::ScenePoint text_item_raw_centre(std::uint32_t self);

} // namespace x2::native

#endif
