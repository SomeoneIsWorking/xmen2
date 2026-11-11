#ifndef X2_LOBBY_FRIEND_CARDS_HPP
#define X2_LOBBY_FRIEND_CARDS_HPP

#include "x86rt.h"

#include <cstdint>
#include <string_view>

/* The online lobby's friend rows (friend_code_rows.hpp), drawn as PDA option
   cards under Host Game and Join Game at the retail rows' pitch, with the
   row's state right of its label. The lobby's scene places only its own two
   rows, so the rows' items have no node and the cards and text are drawn
   when Join Game's label draws. The pad reaches them through the derived
   menu's focus chain; a pointer on a card is taken here. */
namespace x2::native {

/* A text item's draw on CMenuCampaignLobby: true for a friend row, which
   draws nothing itself; Join Game's draw puts up the cards and returns false
   so Join Game draws too. */
bool lobby_friend_cards_draw(const CPU &cpu, std::uint32_t self);
/* True when `name` is a friend row of CMenuCampaignLobby, whose card is its
   finger area. */
bool lobby_friend_cards_has_card(std::string_view menu_class,
                                 std::string_view name);
/* A text item's destructor: the cards' art goes with the first row. */
void lobby_friend_cards_destroy(const CPU &cpu, std::uint32_t self);

} // namespace x2::native

#endif
