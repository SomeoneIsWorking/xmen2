#ifndef X2_MENU_ACTION_HPP
#define X2_MENU_ACTION_HPP

#include <cstdint>
#include <optional>
#include <string_view>

namespace x2::native {

/* A menu command as the menus' pointer handlers post it for the menu's player
   (CPopupDialog::onMouse 0x005ea2c0, CMenuItem::onMouse 0x005bc1b0). */
enum class MenuAction : std::uint32_t {
  accept = 4u,
  subtract = 8u,
  ok = 0x14u,
  back = 0x15u,
  next = 0x16u,
  prev = 0x17u,
  other = 0x18u,
};

/* What a text line's `$MENU_*` token posts when CPopupDialog::onMouse
   releases on it; nothing when it names no action. */
std::optional<MenuAction> menu_token_action(std::string_view text);

/* Queues `action` for the menu's player, read on the next menu update. */
void post_menu_action(MenuAction action);

} // namespace x2::native

#endif
