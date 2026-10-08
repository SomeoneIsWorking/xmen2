#include "boot_splash_policy.h"
#include "../config/environment.h"
#include "guest_memory.h"
#include "x2_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SPLASH_REFUSAL_WINDOW 16u

namespace x2::native {
namespace {

struct {
  int pending;
  unsigned window;
  unsigned long traced;
} g_splash;

} // namespace

void boot_splash_trace(std::uint32_t command) {
  if (!config_override_get(x2::config::ConfigOverride::BootCmdTrace) ||
      !command)
    return;
  if (g_splash.traced >= 40u)
    return;
  g_splash.traced++;
  x2_log_error("BOOT CMD %lu: \"%s\"\n", g_splash.traced,
               (const char *)guest_memory_const_pointer(command));
  if (g_splash.traced == 40u)
    x2_log_error("BOOT CMD: trace window closed at 40 command(s); "
                 "further commands untraced.\n");
}

void boot_splash_arm() {
  g_splash.pending = 1;
  g_splash.window = SPLASH_REFUSAL_WINDOW;
}

int boot_splash_refuse(std::uint32_t command) {
  if (!g_splash.pending)
    return 0;
  if (command &&
      !strcmp(guest_memory_as<const char>(command), "openmenu loading")) {
    g_splash.pending = 0;
    x2_log_error("BOOT SPLASH: refused \"openmenu loading\" after "
                 "the boot-mode dispatch, so the boot splash never "
                 "renders; the menu map load shows nothing instead.\n");
    return 1;
  }
  if (--g_splash.window == 0) {
    g_splash.pending = 0;
    x2_log_error("BOOT SPLASH: no \"openmenu loading\" arrived within "
                 "%u command(s) of the boot-mode dispatch; refusal "
                 "window expired and later loading screens are "
                 "untouched.\n",
                 SPLASH_REFUSAL_WINDOW);
  }
  return 0;
}

} // namespace x2::native
