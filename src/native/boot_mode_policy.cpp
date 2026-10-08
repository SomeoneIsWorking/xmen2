#include "boot_mode_policy.h"

#include <string.h>

#define BOOT_INTRO_COMMAND "runscript menus/intro_normal"

namespace x2::native {

BootModeDecision boot_mode_decide(X2BootMode requested,
                                  int latest_save_available) {
  BootModeDecision decision;
  decision.requested = requested;
  decision.effective =
      (unsigned)requested <= X2_BOOT_CONTINUE ? requested : X2_BOOT_NORMAL;
  decision.fell_back_to_menu = 0;
  if (decision.effective == X2_BOOT_CONTINUE && !latest_save_available) {
    decision.effective = X2_BOOT_MENU;
    decision.fell_back_to_menu = 1;
  }
  return decision;
}

int boot_mode_is_intro_command(const char *command) {
  return command && strcmp(command, BOOT_INTRO_COMMAND) == 0;
}

} // namespace x2::native
