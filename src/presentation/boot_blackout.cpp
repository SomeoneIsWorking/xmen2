#include "boot_blackout.h"
#include "../native/x2_log.h"

#include "guest_clock.h"

#include <stdio.h>

#define BLACKOUT_TIMEOUT_S 30.0
#define BLACKOUT_MAX_FRAMES 5000u

static struct {
  int armed;
  double armed_at_s;
  unsigned long frames_blacked;
  char mode[32];
  char ended[96];
} g_blackout;

void x2_boot_blackout_arm(const char *mode_name) {
  if (g_blackout.armed)
    return;
  g_blackout.armed = 1;
  g_blackout.armed_at_s = guest_clock_elapsed_s();
  g_blackout.frames_blacked = 0;
  g_blackout.ended[0] = 0;
  snprintf(g_blackout.mode, sizeof g_blackout.mode, "%s",
           mode_name ? mode_name : "?");
  x2_log_info("boot blackout: armed for the %s boot -- branding and loading "
              "screens present black until the destination map is up (self "
              "expires at %.0fs or %lu frames).\n",
              g_blackout.mode, BLACKOUT_TIMEOUT_S,
              (unsigned long)BLACKOUT_MAX_FRAMES);
}

void x2_boot_blackout_disarm(const char *why) {
  if (!g_blackout.armed)
    return;
  g_blackout.armed = 0;
  snprintf(g_blackout.ended, sizeof g_blackout.ended, "%s",
           why ? why : "disarmed");
  x2_log_info("boot blackout: lifted after %lu frame(s) -- %s.\n",
              g_blackout.frames_blacked, g_blackout.ended);
}

int x2_boot_blackout_active(void) {
  if (!g_blackout.armed)
    return 0;
  if (g_blackout.frames_blacked >= BLACKOUT_MAX_FRAMES ||
      guest_clock_elapsed_s() - g_blackout.armed_at_s >= BLACKOUT_TIMEOUT_S) {
    x2_boot_blackout_disarm(
        "SELF-EXPIRED without a map return -- the boot load never "
        "completed or its return was never observed");
    return 0;
  }
  return 1;
}

size_t x2_boot_blackout_report(char *out, size_t size) {
  int wrote;
  if (!out || !size)
    return 0;
  wrote = snprintf(out, size,
                   "boot blackout: %s (%s boot), %lu frame(s) presented "
                   "black%s%s\n",
                   g_blackout.armed ? "ARMED" : "closed", g_blackout.mode,
                   g_blackout.frames_blacked, g_blackout.ended[0] ? "; " : "",
                   g_blackout.ended[0] ? g_blackout.ended : "");
  if (wrote < 0)
    return 0;
  if ((size_t)wrote >= size)
    return size - 1u;
  return (size_t)wrote;
}

void x2_boot_blackout_frame_presented(void) {
  if (g_blackout.armed)
    g_blackout.frames_blacked++;
}
