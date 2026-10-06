#ifndef X2_BOOT_MODE_POLICY_H
#define X2_BOOT_MODE_POLICY_H

#include "boot_mode.h"

typedef struct {
  X2BootMode requested;
  X2BootMode effective;
  int fell_back_to_menu;
} X2BootModeDecision;

/* Resolve only policy. The caller owns save discovery and guest dispatch. */
X2BootModeDecision x2_boot_mode_decide(X2BootMode requested,
                                       int latest_save_available);
int x2_boot_mode_is_intro_command(const char *command);

#endif
