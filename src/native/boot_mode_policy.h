#pragma once

#include "boot_mode.h"

namespace x2::native {

struct BootModeDecision {
  X2BootMode requested;
  X2BootMode effective;
  int fell_back_to_menu;
};

/* Resolve only policy. The caller owns save discovery and guest dispatch. */
BootModeDecision boot_mode_decide(X2BootMode requested,
                                  int latest_save_available);
int boot_mode_is_intro_command(const char *command);

} // namespace x2::native
