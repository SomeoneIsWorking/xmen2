#include "boot_splash_policy.h"
#include "../config/environment.h"
#include "guest_memory.h"
#include "x2_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SPLASH_REFUSAL_WINDOW 16u

static struct {
  int pending;
  unsigned window;
  unsigned long traced;
} g_splash;

void x2_boot_splash_trace(uint32_t command) {
  if (!x2_config_override_get(kX2ConfigBootCmdTrace) || !command)
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

void x2_boot_splash_arm(void) {
  g_splash.pending = 1;
  g_splash.window = SPLASH_REFUSAL_WINDOW;
}

int x2_boot_splash_refuse(uint32_t command) {
  if (!g_splash.pending)
    return 0;
  if (command &&
      !strcmp(guest_memory_const_pointer(command), "openmenu loading")) {
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
