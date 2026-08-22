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

void x2_player_participation_policy_init(X2PlayerParticipationPolicy *policy);
void x2_player_participation_policy_configure(
    X2PlayerParticipationPolicy *policy, uint8_t eligible_players);
void x2_player_participation_policy_note_start(
    X2PlayerParticipationPolicy *policy, unsigned player, int down);
X2PlayerParticipationTransition x2_player_participation_policy_consume(
    X2PlayerParticipationPolicy *policy);

#endif
