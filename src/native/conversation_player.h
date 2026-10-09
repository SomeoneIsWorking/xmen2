#pragma once

#include <cstdint>

struct X86pCpu;

namespace x2::native {

enum ConversationPlayerState {
  CONVERSATION_PLAYER_INACTIVE,
  CONVERSATION_PLAYER_WAITING,
  CONVERSATION_PLAYER_DETERMINISTIC,
  CONVERSATION_PLAYER_CHOICE,
  CONVERSATION_PLAYER_UNREADABLE
};

struct ConversationPlayerSelection {
  uint32_t manager;
  uint32_t choose_response;
  uint32_t selected;
  uint32_t line_presenter;
};

/* Conversation is a payload of an authored scene, not its player.  These
 * functions expose the retail manager's current yield and deterministic
 * chooseResponse transition to the cutscene-owned dialogue gate. */
ConversationPlayerState conversation_player_state(struct X86pCpu *cpu);
int conversation_player_selection(struct X86pCpu *cpu,
                                  ConversationPlayerSelection *out);

} // namespace x2::native
