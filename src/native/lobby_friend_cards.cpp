#include "lobby_friend_cards.hpp"

#include "../input/touch_cards.hpp"
#include "../input/touch_runtime.h"
#include "friend_code_rows.hpp"
#include "guest_body.h"
#include "guest_call.hpp"
#include "guest_modules.h"
#include "menu_item_access.hpp"
#include "menu_item_memory.hpp"
#include "menu_scene.hpp"
#include "menu_text.hpp"
#include "option_card.hpp"
#include "retail_menu_model.hpp"
#include "x86rt_native.h"

#include <array>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

namespace item = x2::native::menu_item;
namespace menu_scene = x2::native::menu_scene;
using x2::input::SceneBox;
using x2::native::FriendRow;
using x2::presentation::ScenePoint;

constexpr std::string_view kLobby = "CMenuCampaignLobby";
/* CMenuCampaignLobby::onMouse (vtable 0x0069f404 +0x80): (x, z, _, message,
   wheel), ret 0x14. */
constexpr std::uint32_t kOnMouse = 0x005b03b0u;
constexpr std::uint32_t kButtonDown = 0x201u;
constexpr std::uint32_t kButtonUp = 0x202u;
constexpr std::uint32_t kPointerMove = 0x200u;

/* The retail rows, and the panel whose box gives a card's width and height
   (a model item's box is right across but not in the scene's z). */
constexpr std::string_view kHostRow = "text_hostgame";
constexpr std::string_view kJoinRow = "text_joingame";
constexpr std::string_view kPanel = "panel02";
constexpr float kCardDepth = 5.0F;
/* Text inset from a card's ends, in card heights, as the retail rows' text
   sits on theirs; the label's share of the text width. */
constexpr float kTextPad = 0.4F;
constexpr float kLabelShare = 0.45F;

float middle(const SceneBox &box) { return 0.5F * (box.bottom + box.top); }

class LobbyCards {
public:
  bool draw(const CPU &cpu, std::uint32_t self) {
    const std::uint32_t menu = RD32(self + item::kMenu);
    x2::guest::GuestCall call(cpu);
    Lobby &lobby = lobby_for(call, menu);
    for (const Row &row : lobby.rows) {
      if (row.item == self) {
        return true;
      }
    }
    if (self != lobby.join || lobby.rows.empty()) {
      return false;
    }
    menu_scene::Vectors vectors;
    if (!vectors || lobby.host == 0u || lobby.panel == 0u) {
      return false;
    }
    const SceneBox host = item::box(lobby.host);
    const SceneBox join = item::box(lobby.join);
    const SceneBox panel = item::box(lobby.panel);
    const float pitch = middle(host) - middle(join);
    const float half = 0.5F * panel.height();
    const float depth = static_cast<float>(
        static_cast<std::int16_t>(RD16(self + item::kDepth)));
    const std::uint32_t focus = RD32(menu + x2::menu::kMenuFocus);
    for (std::size_t index = 0; index < lobby.rows.size(); ++index) {
      Row &row = lobby.rows[index];
      const float centre =
          middle(join) - pitch * static_cast<float>(index + 1u);
      row.card = {panel.left, centre - half, panel.right, centre + half};
      row.area = {panel.left, centre - 0.5F * pitch, panel.right,
                  centre + 0.5F * pitch};
      item::set_box(row.item, row.card);
      const bool focused = focus == row.item;
      row.art.show(call, vectors, row.card, depth + kCardDepth, focused);
      x2::native::menu_text::label_and_value(
          call, self, row.item, row.card, row.text->label,
          x2::native::friend_row_value(row.text->row), focused, kTextPad,
          kLabelShare);
    }
    return false;
  }

  void destroy(const CPU &cpu, std::uint32_t self) {
    for (auto at = lobbies_.begin(); at != lobbies_.end(); ++at) {
      Lobby &lobby = at->second;
      if (lobby.rows.empty() || lobby.rows.front().item != self) {
        continue;
      }
      x2::guest::GuestCall call(cpu);
      for (Row &row : lobby.rows) {
        row.art.release(call);
      }
      lobbies_.erase(at);
      return;
    }
  }

  /* True when the pointer message was over a card, which takes it. */
  bool pointer(const CPU &cpu, std::uint32_t menu, ScenePoint point,
               std::uint32_t message) {
    const auto found = lobbies_.find(menu);
    if (found == lobbies_.end()) {
      return false;
    }
    Lobby &lobby = found->second;
    const Row *over = nullptr;
    for (const Row &row : lobby.rows) {
      if (row.area.contains(point)) {
        over = &row;
      }
    }
    x2::guest::GuestCall call(cpu);
    if (message == kButtonDown) {
      pressed_ = over != nullptr ? over->item : 0u;
      return over != nullptr;
    }
    if (message == kButtonUp) {
      const bool chose = over != nullptr && pressed_ == over->item;
      pressed_ = 0u;
      if (chose) {
        item::focus(call, menu, over->item);
        x2::native::friend_row_choose(over->text->row);
      }
      return over != nullptr;
    }
    if (message == kPointerMove && over != nullptr &&
        x2::input::touch_runtime_active() == 0 &&
        RD32(menu + x2::menu::kMenuFocus) != over->item) {
      /* A mouse over a card focuses its row, as on the retail rows. */
      item::focus(call, menu, over->item);
    }
    return over != nullptr;
  }

private:
  struct Row {
    const x2::native::FriendRowText *text = nullptr;
    std::uint32_t item = 0u;
    x2::native::OptionCard art;
    SceneBox card;
    SceneBox area;
  };
  struct Lobby {
    std::vector<Row> rows;
    std::uint32_t host = 0u;
    std::uint32_t join = 0u;
    std::uint32_t panel = 0u;
  };

  Lobby &lobby_for(x2::guest::GuestCall &call, std::uint32_t menu) {
    const auto found = lobbies_.find(menu);
    if (found != lobbies_.end()) {
      return found->second;
    }
    Lobby &lobby = lobbies_[menu];
    lobby.host = item::named(call, menu, kHostRow);
    lobby.join = item::named(call, menu, kJoinRow);
    lobby.panel = item::named(call, menu, kPanel);
    menu_scene::Vectors vectors;
    for (const auto &text : x2::native::kFriendRows) {
      Row row;
      row.text = &text;
      row.item = item::named(call, menu, text.item);
      if (row.item == 0u) {
        continue;
      }
      if (vectors) {
        row.art.load(call, vectors, true);
      }
      lobby.rows.push_back(row);
    }
    return lobby;
  }

  std::unordered_map<std::uint32_t, Lobby> lobbies_;
  std::uint32_t pressed_ = 0u;
};

LobbyCards g_cards;

bool on_lobby(std::uint32_t menu) {
  return std::string_view(x2::menu::classify_menu(
             RD32(menu), x2::native::guest_exe_base())) == kLobby;
}

} // namespace

bool x2::native::lobby_friend_cards_draw(const CPU &cpu, std::uint32_t self) {
  return on_lobby(RD32(self + item::kMenu)) && g_cards.draw(cpu, self);
}

bool x2::native::lobby_friend_cards_has_card(std::string_view menu_class,
                                             std::string_view name) {
  return menu_class == kLobby && friend_row_of_item(name).has_value();
}

void x2::native::lobby_friend_cards_destroy(const CPU &cpu,
                                            std::uint32_t self) {
  g_cards.destroy(cpu, self);
}

extern "C" void x2_override_005b03b0(CPU *cpu) {
  const std::uint32_t esp = cpu->reg[kX86pEsp];
  const ScenePoint point{
      static_cast<float>(static_cast<std::int32_t>(RD32(esp + 4u))),
      static_cast<float>(static_cast<std::int32_t>(RD32(esp + 8u)))};
  if (g_cards.pointer(*cpu, cpu->reg[kX86pEcx], point, RD32(esp + 16u))) {
    cpu->reg[kX86pEax] &= ~0xffu;
    cpu->reg[kX86pEsp] += 4u + 20u;
    return;
  }
  x86_guest_body(cpu, "XMen2.exe", kOnMouse);
}

__attribute__((constructor)) static void x2_lobby_friend_cards_register(void) {
  x86_register_override("XMen2.exe", kOnMouse, x2_override_005b03b0);
}
