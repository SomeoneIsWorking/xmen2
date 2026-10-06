#ifndef X2_PLAYER_PARTICIPATION_POLICY_H
#define X2_PLAYER_PARTICIPATION_POLICY_H

#include <stdint.h>

#define X2_PARTICIPATION_PLAYERS 4u

typedef struct {
  uint8_t eligible;
  uint8_t start_down;
  uint8_t pending_join;
  uint8_t pending_leave;
  uint8_t configured;
} X2PlayerParticipationPolicy;

typedef struct {
  uint8_t join;
  uint8_t leave;
} X2PlayerParticipationTransition;

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
typedef struct {
  int32_t controller_of_player[X2_PARTICIPATION_PLAYERS];
  /* Bit c: controller c belongs to this machine (below the manager's count
     at +0x30 and not flagged remote). */
  uint8_t local_controllers;
} X2PlayerSeatMap;

/* The game players driven by the local seats in `seats`. */
uint8_t x2_player_seats_to_players(const X2PlayerSeatMap *map, uint8_t seats);

/* The game players this machine may join or evict: those on a local
   controller. A remote player is the network session's to manage. */
uint8_t x2_player_seats_governed(const X2PlayerSeatMap *map);

void x2_player_participation_policy_init(X2PlayerParticipationPolicy *policy);
void x2_player_participation_policy_configure(
    X2PlayerParticipationPolicy *policy, uint8_t eligible_players);
void x2_player_participation_policy_note_start(
    X2PlayerParticipationPolicy *policy, unsigned player, int down);
X2PlayerParticipationTransition
x2_player_participation_policy_consume(X2PlayerParticipationPolicy *policy);

#endif
