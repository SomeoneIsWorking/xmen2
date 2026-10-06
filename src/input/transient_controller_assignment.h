#ifndef X2_TRANSIENT_CONTROLLER_ASSIGNMENT_H
#define X2_TRANSIENT_CONTROLLER_ASSIGNMENT_H

#ifdef __cplusplus
extern "C" {
#endif

/* Process-lifetime assignments for pads without a persistent serial/path.
   They bind the current live GUID, never an inventory slot, and are never
   part of X2Settings serialization. */
int x2_transient_controller_assign(int pad, unsigned player);
void x2_transient_controller_clear_player(unsigned player);
int x2_transient_controller_has_assignment(unsigned player);
int x2_transient_controller_resolve(unsigned player);
int x2_transient_controller_player_for_pad(int pad);
const char *x2_transient_controller_id(unsigned player);
void x2_transient_controller_reset(void);

#ifdef __cplusplus
}
#endif

#endif
