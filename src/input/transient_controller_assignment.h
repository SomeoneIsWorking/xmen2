#ifndef X2_TRANSIENT_CONTROLLER_ASSIGNMENT_H
#define X2_TRANSIENT_CONTROLLER_ASSIGNMENT_H

/* Process-lifetime assignments for pads without a persistent serial/path.
   They bind the current live GUID, never an inventory slot, and are never
   part of X2Settings serialization. Assigning to a held player moves the
   displaced pad to the seat the assigned pad left. */
int x2_transient_controller_assign(int pad, unsigned player);
/* Moves `from`'s session assignment to an unassigned `to`. */
int x2_transient_controller_move(unsigned from, unsigned to);
void x2_transient_controller_clear_player(unsigned player);
int x2_transient_controller_has_assignment(unsigned player);
int x2_transient_controller_resolve(unsigned player);
int x2_transient_controller_player_for_pad(int pad);
const char *x2_transient_controller_id(unsigned player);
void x2_transient_controller_reset(void);

#endif
