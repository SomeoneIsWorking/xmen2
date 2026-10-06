#ifndef X2_POWER_SLOTS_H
#define X2_POWER_SLOTS_H

/* Host path of the IGB atlas the player's power buttons index, or "" when no
   hero with powers is in play. Written by the guest's HUD update and read on
   the same thread; valid until that update runs again. */
const char *x2_power_slots_atlas(void);

#endif /* X2_POWER_SLOTS_H */
