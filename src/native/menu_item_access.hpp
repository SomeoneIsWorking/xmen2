#ifndef X2_MENU_ITEM_ACCESS_HPP
#define X2_MENU_ITEM_ACCESS_HPP

#include "../input/touch_cards.hpp"
#include "guest_call.hpp"
#include "guest_modules.h"
#include "menu_item_memory.hpp"
#include "x86rt.h"

#include <cmath>
#include <cstdint>
#include <string_view>

/* A live CMenuItem's box and lookup by name (menu_item_memory.hpp). */
namespace x2::native::menu_item {

/* The item's hit box in the scene, z up. */
inline input::SceneBox box(std::uint32_t self) {
  const auto word = [](std::uint32_t at) {
    return static_cast<float>(static_cast<std::int16_t>(RD16(at)));
  };
  const float left = word(self + kBoxLeft);
  const float top = word(self + kBoxTop);
  return {left, top - word(self + kBoxHeight), left + word(self + kBoxWidth),
          top};
}

/* Puts the item's hit box at `scene`, its local top at 0. */
inline void set_box(std::uint32_t self, const input::SceneBox &scene) {
  const auto word = [](float v) {
    return static_cast<std::uint16_t>(
        static_cast<std::int16_t>(std::lround(v)));
  };
  WR16(self + kBoxLeft, word(scene.left));
  WR16(self + kBoxTop, word(scene.top));
  WR16(self + kBoxWidth, word(scene.width()));
  WR16(self + kBoxHeight, word(scene.height()));
  WR16(self + kLocalTop, 0u);
}

/* `menu`'s item called `name`, or 0. */
inline std::uint32_t named(guest::GuestCall &call, std::uint32_t menu,
                           std::string_view name) {
  const guest::GuestText text(name);
  return text ? call.thiscall(guest_exe_base() + kItemByNameRva, menu,
                              {text.address()})
              : 0u;
}

/* CMenu slot 4, CMenu::setFocus: focuses `menu`'s `item` as a pointer or
   the pad would. */
inline constexpr std::uint32_t kMenuSetFocusSlot = 0x04u;
inline void focus(guest::GuestCall &call, std::uint32_t menu,
                  std::uint32_t item) {
  call.virtual_call(menu, kMenuSetFocusSlot, {item});
}

} // namespace x2::native::menu_item

#endif
