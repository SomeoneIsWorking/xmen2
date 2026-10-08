#pragma once

namespace x2::input {

/* Process-lifetime assignments for pads without a persistent serial/path.
   They bind the current live GUID, never an inventory slot, and are never
   part of Settings serialization. Assigning to a held player moves the
   displaced pad to the seat the assigned pad left. */
int transient_controller_assign(int pad, unsigned player);
/* Moves `from`'s session assignment to an unassigned `to`. */
int transient_controller_move(unsigned from, unsigned to);
void transient_controller_clear_player(unsigned player);
int transient_controller_has_assignment(unsigned player);
int transient_controller_resolve(unsigned player);
int transient_controller_player_for_pad(int pad);
const char *transient_controller_id(unsigned player);
void transient_controller_reset(void);

} // namespace x2::input
