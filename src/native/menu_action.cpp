#include "menu_action.hpp"

#include "guest_modules.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include <algorithm>
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

/* CMenuCodex::onMouse 0x005b0c80 publishes action 9 on desctext3;
   CMenuTeam's update holds it while the pointer is on desctext4. */
constexpr std::array<std::pair<std::string_view, std::string_view>, 2>
    kMenuTokens{{
        {"CMenuCodex", "$MENU_DETAILS"},
        {"CMenuTeam", "$MENU_DETAILS"},
    }};

/* The pad binds it to RB (FUN_00619c40, binding row 10). */
constexpr std::string_view kDropToken = "$MENU_DROP";

} // namespace

bool menu_handles_token(std::string_view menu_class, std::string_view token) {
  return std::find(kMenuTokens.begin(), kMenuTokens.end(),
                   std::pair{menu_class, token}) != kMenuTokens.end();
}

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

std::optional<MenuAction> touch_token_action(std::string_view text) {
  if (text.find(kDropToken) == std::string_view::npos) {
    return std::nullopt;
  }
  return MenuAction::drop;
}

std::string prompt_label(std::string_view text, std::string_view token) {
  const std::size_t at = text.find(token);
  if (at == std::string_view::npos) {
    return std::string(text);
  }
  std::size_t end = text.find(' ', at);
  end = end == std::string_view::npos ? text.size() : end + 1u;
  return std::string(text.substr(0, at)) + std::string(text.substr(end));
}

std::string menu_token_label(std::string_view text) {
  return prompt_label(text, "$MENU");
}

void post_menu_action(MenuAction action) {
  const std::uint32_t player = RD32(exe_linked(kMenuPlayer));
  WR8(exe_linked(kMenuActionPending) + player, 1u);
  WR32(exe_linked(kMenuActions) + player * 4u,
       static_cast<std::uint32_t>(action));
}

} // namespace x2::native
