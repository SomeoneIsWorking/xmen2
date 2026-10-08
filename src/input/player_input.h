#pragma once

#include <cstdint>

struct X86pCpu;

namespace x2::input {

/* Publish persisted player/device ownership and mappings into the game's
   master, working and menu binding sets. Safe at the DirectInput frame pump. */
void player_input_sync(struct X86pCpu *cpu);

/* The last assignment published into the guest. Prompt mode must follow this
   resolved owner, not whether an unrelated or unassigned pad is connected. */
int player_input_uses_gamepad(unsigned player);
int player_input_resolved_pad(unsigned player);
void player_input_note_keyboard_state(const unsigned char *state,
                                      unsigned bytes);
void player_input_note_gamepad_activity(int pad);
void player_input_note_gamepad_state(int pad, const unsigned char *state,
                                     unsigned bytes);
int player_input_pad_is_active_source(int pad);

/* The game's own slot-0 binding for `row`, read from its master set before
   any profile override is published. 0 until the guest has built that set. */
int player_input_game_keyboard_binding(unsigned row, uint32_t *kind,
                                       uint32_t *code);

} // namespace x2::input
