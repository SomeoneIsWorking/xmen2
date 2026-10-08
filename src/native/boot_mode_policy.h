#pragma once

#include "boot_mode.h"

namespace x2::native {

struct BootModeDecision {
  x2::config::BootMode requested;
  x2::config::BootMode effective;
  int fell_back_to_menu;
};

/* Resolve only policy. The caller owns save discovery and guest dispatch. */
BootModeDecision boot_mode_decide(x2::config::BootMode requested,
                                  int latest_save_available);
int boot_mode_is_intro_command(const char *command);

} // namespace x2::native
