#ifndef X2_MENU_ACTION_HPP
#define X2_MENU_ACTION_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace x2::native {

/* A menu command as the menus' pointer handlers post it for the menu's player
   (CPopupDialog::onMouse 0x005ea2c0, CMenuItem::onMouse 0x005bc1b0). */
enum class MenuAction : std::uint32_t {
  accept = 4u,
  /* The pad's B, beside back. */
  cancel = 5u,
  subtract = 8u,
  /* LT: the team's skill Details while held, the codex's Details. */
  details = 9u,
  /* RB: the team's Assign and Drop. */
  drop = 0xdu,
  ok = 0x14u,
  back = 0x15u,
  next = 0x16u,
  prev = 0x17u,
  other = 0x18u,
};

/* What a text line's `$MENU_*` token posts when CPopupDialog::onMouse
   releases on it; nothing when it names no action. */
std::optional<MenuAction> menu_token_action(std::string_view text);
/* What a touch button for a prompt posts itself, a token no retail onMouse
   publishes: `$MENU_DROP`. */
std::optional<MenuAction> touch_token_action(std::string_view text);
/* Whether `menu_class`'s own onMouse acts on a click on the prompt carrying
   `token`, one CMenuItemText::onMouse does not publish. */
bool menu_handles_token(std::string_view menu_class, std::string_view token);

/* `text` without the run of tokens starting at `token` and the space after
   it: what a touch button for the prompt reads. */
std::string prompt_label(std::string_view text, std::string_view token);
/* `text` without its `$MENU_*` token. */
std::string menu_token_label(std::string_view text);

/* Queues `action` for the menu's player, read on the next menu update. */
void post_menu_action(MenuAction action);

} // namespace x2::native

#endif
