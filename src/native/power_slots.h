#pragma once

namespace x2::native {

/* Host path of the IGB atlas the player's power buttons index, or "" when no
   hero with powers is in play. Written by the guest's HUD update and read on
   the same thread; valid until that update runs again. */
const char *power_slots_atlas(void);

} // namespace x2::native
