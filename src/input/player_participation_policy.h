#pragma once

#include <cstdint>

namespace x2::input {

inline constexpr unsigned kParticipationPlayers = 4u;

struct PlayerParticipationPolicy {
  uint8_t eligible;
  uint8_t start_down;
  uint8_t pending_join;
  uint8_t pending_leave;
  uint8_t configured;
};

struct PlayerParticipationTransition {
  uint8_t join;
  uint8_t leave;
};

/*
 * The policy above speaks in LOCAL SEATS: seat n is controller bank n, the
 * keyboard profile or pad the port's settings give "Player n+1". The retail
 * participation manager speaks in GAME PLAYERS. Locally they are the same
 * number, but a network session remaps them through the game's own
 * player -> controller table (player manager 0x00551ed0, ints at +4): a LAN
 * client's keyboard, controller 0, drives game player 1 behind the host's
 * player 0, and the host sees that client as player 1 on a controller it
 * flags remote (+0x14 + controller). Translating through the map is what lets
 * the port govern its own seats without evicting a network player.
 */
struct PlayerSeatMap {
  int32_t controller_of_player[kParticipationPlayers];
  /* Bit c: controller c belongs to this machine (below the manager's count
     at +0x30 and not flagged remote). */
  uint8_t local_controllers;
};

/* The game players driven by the local seats in `seats`. */
uint8_t player_seats_to_players(const PlayerSeatMap *map, uint8_t seats);

/* The game players this machine may join or evict: those on a local
   controller. A remote player is the network session's to manage. */
uint8_t player_seats_governed(const PlayerSeatMap *map);

void player_participation_policy_init(PlayerParticipationPolicy *policy);
void player_participation_policy_configure(PlayerParticipationPolicy *policy,
                                           uint8_t eligible_players);
void player_participation_policy_note_start(PlayerParticipationPolicy *policy,
                                            unsigned player, int down);
PlayerParticipationTransition
player_participation_policy_consume(PlayerParticipationPolicy *policy);

} // namespace x2::input
