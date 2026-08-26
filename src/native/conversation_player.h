#ifndef X2_CONVERSATION_PLAYER_H
#define X2_CONVERSATION_PLAYER_H

#include <stdint.h>

struct CPU;

typedef enum ConversationPlayerState {
    CONVERSATION_PLAYER_INACTIVE,
    CONVERSATION_PLAYER_WAITING,
    CONVERSATION_PLAYER_DETERMINISTIC,
    CONVERSATION_PLAYER_CHOICE,
    CONVERSATION_PLAYER_UNREADABLE
} ConversationPlayerState;

/* Conversation is a payload of an authored scene, not its player.  These
 * functions expose the retail manager's current yield and its existing
 * chooseResponse transition to the BehavEd cutscene player. */
ConversationPlayerState conversation_player_state(struct CPU *cpu);
int conversation_player_advance(struct CPU *cpu);

#endif
