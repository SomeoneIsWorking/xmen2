#ifndef X2_PLAYER_INPUT_H
#define X2_PLAYER_INPUT_H

struct CPU;

/* Publish persisted player/device ownership and mappings into the game's
   master, working and menu binding sets. Safe at the DirectInput frame pump. */
void x2_player_input_sync(struct CPU *cpu);

/* The last assignment published into the guest. Prompt mode must follow this
   resolved owner, not whether an unrelated or unassigned pad is connected. */
int x2_player_input_uses_gamepad(unsigned player);
int x2_player_input_resolved_pad(unsigned player);
void x2_player_input_note_keyboard_state(const unsigned char *state,
                                         unsigned bytes);
void x2_player_input_note_gamepad_activity(int pad);
int x2_player_input_pad_is_active_source(int pad);

#endif
