#include "boot_mode.h"

#include <string.h>

const char *x2_boot_mode_name(X2BootMode mode) {
  static const char *const NAME[] = {"normal", "menu", "continue"};
  return (unsigned)mode <= X2_BOOT_CONTINUE ? NAME[mode] : "invalid";
}

const char *x2_boot_mode_label(X2BootMode mode) {
  static const char *const LABEL[] = {"Boot normally", "Boot to menu",
                                      "Boot to continue"};
  return (unsigned)mode <= X2_BOOT_CONTINUE ? LABEL[mode] : "Invalid boot mode";
}

int x2_boot_mode_parse(const char *text, X2BootMode *mode) {
  X2BootMode i;
  for (i = X2_BOOT_NORMAL; i <= X2_BOOT_CONTINUE; i++)
    if (strcmp(text, x2_boot_mode_name(i)) == 0) {
      if (mode)
        *mode = i;
      return 1;
    }
  return 0;
}
