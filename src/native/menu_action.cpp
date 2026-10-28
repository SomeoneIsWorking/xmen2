#include "menu_action.hpp"

#include "guest_modules.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include <array>
#include <utility>

namespace x2::native {
namespace {

constexpr std::uint32_t kMenuPlayer = 0x006f3920u;
constexpr std::uint32_t kMenuActions = 0x00a09f54u;
constexpr std::uint32_t kMenuActionPending = 0x00a09fa0u;

/* onMouse's own order: the first `$MENU` in the line, matched by prefix. */
constexpr std::array<std::pair<std::string_view, MenuAction>, 7> kTokens{{
    {"$MENU_OK", MenuAction::ok},
    {"$MENU_BACK", MenuAction::back},
    {"$MENU_NEXT", MenuAction::next},
    {"$MENU_PREV", MenuAction::prev},
    {"$MENU_OTHER", MenuAction::other},
    {"$MENU_ACCEPT", MenuAction::accept},
    {"$MENU_SUBTRACT", MenuAction::subtract},
}};

} // namespace

std::optional<MenuAction> menu_token_action(std::string_view text) {
  const std::size_t at = text.find("$MENU");
  if (at == std::string_view::npos) {
    return std::nullopt;
  }
  const std::string_view token = text.substr(at);
  for (const auto &[name, action] : kTokens) {
    if (token.substr(0, name.size()) == name) {
      return action;
    }
  }
  return std::nullopt;
}

void post_menu_action(MenuAction action) {
  const std::uint32_t player = RD32(exe_linked(kMenuPlayer));
  WR8(exe_linked(kMenuActionPending) + player, 1u);
  WR32(exe_linked(kMenuActions) + player * 4u,
       static_cast<std::uint32_t>(action));
}

} // namespace x2::native
