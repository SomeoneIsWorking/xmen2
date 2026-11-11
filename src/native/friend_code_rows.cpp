#include "friend_code_rows.hpp"

#include "host_clipboard.hpp"
#include "lan_coordinator.hpp"
#include "retail_front_end.hpp"
#include "retail_menu_model.hpp"
#include "shell32.h"
#include "x2_log.h"
#include "x86rt_native.h"

#include "../net/friends.hpp"

#include <lucent/cvar_c.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

namespace x2::native {
namespace {

using net::Friends;

constexpr std::string_view kHost = "text_hostgame";
constexpr std::string_view kJoin = "text_joingame";

xmlb::Node *named(xmlb::Node &root, std::string_view name) {
  for (xmlb::Node &child : root.children) {
    if (child.name == "item" && child.get("name") == name) {
      return &child;
    }
  }
  return nullptr;
}

/* What the rows remember between presses: the code last put on the
   clipboard, a copy waiting for the code being made, and the last refusal. */
class FriendRows {
public:
  std::string value(FriendRow row) {
    go_online();
    const Friends::Shown shown = friends().shown();
    take_pending(shown);
    if (row == FriendRow::YourCode) {
      return your_code(shown);
    }
    return join_by_code(shown);
  }

  void choose(FriendRow row) {
    const Friends::Shown shown = friends().shown();
    code_refusal_.clear();
    join_refusal_.clear();
    if (row == FriendRow::YourCode) {
      copy_or_make(shown);
    } else {
      paste();
    }
  }

private:
  static Friends &friends() { return lan::coordinator().friends(); }

  /* The node starts when the rows are first shown; its identity is user data
     beside the saves. lan.zerotier_roots names a root-set file for a lab. */
  void go_online() {
    if (online_) {
      return;
    }
    online_ = true;
    std::string roots;
    const std::filesystem::path file = lucent_cvar_text("lan.zerotier_roots");
    if (!file.empty()) {
      std::ifstream in{file, std::ios::binary};
      if (!in) {
        x2_log_error("friend rows: no root set at %s\n", file.string().c_str());
        return;
      }
      roots.assign(std::istreambuf_iterator<char>{in},
                   std::istreambuf_iterator<char>{});
    }
    friends().go_online(std::filesystem::path{save_dir()} / "zerotier", roots);
  }

  static bool meeting(const Friends::Shown &shown) {
    return shown.stage == Friends::Stage::Gathering ||
           shown.stage == Friends::Stage::Waiting ||
           shown.stage == Friends::Stage::Connecting;
  }

  void copy_or_make(const Friends::Shown &shown) {
    if (!shown.code.empty()) {
      copy(shown.code);
      return;
    }
    if (meeting(shown)) {
      pending_copy_ = true;
      return;
    }
    std::string error;
    if (!friends().invite(error)) {
      code_refusal_ = error;
      x2_log_info("friend rows: no code: %s\n", error.c_str());
      return;
    }
    copied_.clear();
    pending_copy_ = true;
  }

  void paste() {
    const std::optional<std::string> text = clipboard_text();
    if (!text || text->empty()) {
      join_refusal_ = "Copy a code first";
      x2_log_info("friend rows: Join by code found no text on the "
                  "clipboard\n");
      return;
    }
    std::string error;
    if (!friends().paste(*text, error)) {
      join_refusal_ = error;
      x2_log_info("friend rows: the pasted code was refused: %s\n",
                  error.c_str());
    }
  }

  void copy(const std::string &code) {
    pending_copy_ = false;
    if (clipboard_put(code)) {
      copied_ = code;
      x2_log_info("friend rows: copied this machine's code\n");
    } else {
      code_refusal_ = "The clipboard refused it";
    }
  }

  void take_pending(const Friends::Shown &shown) {
    if (pending_copy_ && !shown.code.empty()) {
      copy(shown.code);
    }
    if (!meeting(shown)) {
      pending_copy_ = false;
    }
  }

  std::string your_code(const Friends::Shown &shown) const {
    if (!code_refusal_.empty()) {
      return code_refusal_;
    }
    switch (shown.stage) {
    case Friends::Stage::Gathering:
      return "Making a code...";
    case Friends::Stage::Waiting:
      return shown.code == copied_ ? "Copied" : "Copy";
    case Friends::Stage::Connecting:
      return "Friend coming...";
    case Friends::Stage::Connected:
      return friend_count(shown);
    case Friends::Stage::Failed:
    case Friends::Stage::Idle:
      break;
    }
    return "Make one";
  }

  std::string join_by_code(const Friends::Shown &shown) const {
    if (!join_refusal_.empty()) {
      return join_refusal_;
    }
    switch (shown.stage) {
    case Friends::Stage::Connecting:
      return "Joining...";
    case Friends::Stage::Connected:
      return friend_count(shown);
    case Friends::Stage::Failed:
      return shown.problem;
    case Friends::Stage::Waiting:
    case Friends::Stage::Gathering:
    case Friends::Stage::Idle:
      break;
    }
    return "Paste";
  }

  static std::string friend_count(const Friends::Shown &shown) {
    return shown.friends == 1
               ? std::string{"1 friend joined"}
               : std::to_string(shown.friends) + " friends joined";
  }

  std::string copied_;
  std::string code_refusal_;
  std::string join_refusal_;
  bool pending_copy_ = false;
  bool online_ = false;
};

FriendRows g_rows;

} // namespace

std::optional<xmlb::Node> derive_lobby_menu(const xmlb::Node &retail,
                                            std::string *why) {
  xmlb::Node root = retail;
  xmlb::Node *host = named(root, kHost);
  xmlb::Node *join = named(root, kJoin);
  if (root.name != "MENU" || root.get("type") != "CAMPAIGN_LOBBY_MENU" ||
      host == nullptr || join == nullptr || host->get("down") != kJoin ||
      join->get("down") != kHost) {
    *why = "no Host Game and Join Game chained to each other";
    return std::nullopt;
  }
  const std::optional<std::string_view> scene = join->get("animtext_scene");
  const std::optional<std::string_view> style = join->get("style");
  if (!scene || !style) {
    *why = "Join Game has no text scene or style";
    return std::nullopt;
  }
  std::vector<xmlb::Node> items;
  for (std::size_t i = 0; i < kFriendRows.size(); ++i) {
    const FriendRowText &row = kFriendRows[i];
    xmlb::Node item{"item", {}, {}};
    item.set("animtext_scene", *scene);
    item.set("down",
             i + 1u < kFriendRows.size() ? kFriendRows[i + 1u].item : kHost);
    item.set("name", row.item);
    item.set("style", *style);
    item.set("text", row.label);
    item.set("textalignx", "TEXT_ALIGN_LEFT");
    item.set("textaligny", "TEXT_ALIGN_CENTER_Y");
    item.set("up", i > 0 ? kFriendRows[i - 1u].item : kJoin);
    item.set("usecmd", kFriendRowCommand);
    items.push_back(std::move(item));
  }
  join->set("down", kFriendRows.front().item);
  host->set("up", kFriendRows.back().item);
  std::size_t at = 0;
  while (&root.children[at] != join) {
    ++at;
  }
  root.children.insert(root.children.begin() + static_cast<long>(at) + 1,
                       items.begin(), items.end());
  return root;
}

std::optional<FriendRow> friend_row_of_item(std::string_view name) {
  for (const FriendRowText &row : kFriendRows) {
    if (row.item == name) {
      return row.row;
    }
  }
  return std::nullopt;
}

std::string friend_row_value(FriendRow row) { return g_rows.value(row); }

void friend_row_choose(FriendRow row) { g_rows.choose(row); }

void friend_row_command(CPU *cpu) {
  const std::uint32_t menu = retail::FrontEnd::active_menu_object(*cpu);
  const std::uint32_t item = menu != 0u ? RD32(menu + menu::kMenuFocus) : 0u;
  const std::string name =
      item != 0u ? menu::read_live_item_name(item) : std::string();
  if (const std::optional<FriendRow> row = friend_row_of_item(name)) {
    friend_row_choose(*row);
  } else {
    x2_log_error("friend rows: `%.*s` ran with \"%s\" focused, which is no "
                 "friend row; nothing was done.\n",
                 static_cast<int>(kFriendRowCommand.size()),
                 kFriendRowCommand.data(), name.c_str());
  }
  /* A BehavEd menu command is a void, no-argument callback ending in RET. */
  cpu->reg[kX86pEsp] += 4u;
}

} // namespace x2::native
