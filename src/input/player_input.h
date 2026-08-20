#ifndef X2_PLAYER_INPUT_H
#define X2_PLAYER_INPUT_H

struct CPU;

/* Publish persisted player/device ownership and mappings into the game's
   master, working and menu binding sets. Safe at the DirectInput frame pump. */
void x2_player_input_sync(struct CPU *cpu);

#endif
