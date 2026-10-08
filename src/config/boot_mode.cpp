#include "boot_mode.h"

#include <string.h>

namespace x2::config {

const char *boot_mode_name(BootMode mode) {
  static const char *const NAME[] = {"normal", "menu", "continue"};
  return (unsigned)mode <= (unsigned)BootMode::Continue ? NAME[(unsigned)mode]
                                                        : "invalid";
}

const char *boot_mode_label(BootMode mode) {
  static const char *const LABEL[] = {"Boot normally", "Boot to menu",
                                      "Boot to continue"};
  return (unsigned)mode <= (unsigned)BootMode::Continue ? LABEL[(unsigned)mode]
                                                        : "Invalid boot mode";
}

int boot_mode_parse(const char *text, BootMode *mode) {
  for (int i = (int)BootMode::Normal; i <= (int)BootMode::Continue; i++) {
    if (strcmp(text, boot_mode_name(static_cast<BootMode>(i))) == 0) {
      if (mode) {
        *mode = static_cast<BootMode>(i);
      }
      return 1;
    }
  }
  return 0;
}

} // namespace x2::config
