#ifndef X2_FRIEND_CODE_ROWS_HPP
#define X2_FRIEND_CODE_ROWS_HPP

#include "x86rt.h"
#include "xmlb_tree.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

/* The online lobby's friend rows (plan #204): "Your code" copies the code
   this machine shows to the clipboard, making one first when there is none,
   and "Join by code" takes the code on the clipboard, an invite or the reply
   to this machine's. They follow Host Game and Join Game in the derived
   ui/menus/campaign_lobby menu, drawn by lobby_friend_cards.hpp, and both run
   `port_friend_row`, which acts on the row the menu has focused. */
namespace x2::native {

enum class FriendRow : std::uint8_t { YourCode, JoinByCode };

struct FriendRowText {
  FriendRow row;
  std::string_view item;
  std::string_view label;
};

inline constexpr std::array<FriendRowText, 2> kFriendRows{{
    {FriendRow::YourCode, "text_friendcode", "Your code"},
    {FriendRow::JoinByCode, "text_joincode", "Join by code"},
}};

/* `retail`'s lobby with the friend rows after Join Game, in the pad's chain
   Host Game, Join Game, Your code, Join by code; nothing when `retail` is
   not the lobby this expects. */
std::optional<xmlb::Node> derive_lobby_menu(const xmlb::Node &retail,
                                            std::string *why);

/* The friend row an item of that name is. */
std::optional<FriendRow> friend_row_of_item(std::string_view name);

/* What the row shows right of its label: the step of this machine's
   meeting, or why the last press did nothing. */
std::string friend_row_value(FriendRow row);

/* A press on the row, from the pad's accept, a click or a tap. */
void friend_row_choose(FriendRow row);

/* The `port_friend_row` command: chooses the focused friend row. */
inline constexpr std::string_view kFriendRowCommand = "port_friend_row";
void friend_row_command(CPU *cpu);

} // namespace x2::native

#endif
