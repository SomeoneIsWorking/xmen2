#include "control_performance_route.h"

#include "control.h"
#include "control_command_bridge.h"
#include "x86_hotep.h"

#include <stdlib.h>
#include <string.h>

void control_performance_reset_route(x2_socket_t fd) {
  char reason[192];
  const int result = control_command_performance_reset(reason, sizeof reason);
  if (result < 0) {
    control_reply_text(fd, 504, "Gateway Timeout",
                       "the guest did not reach an input poll within 10s; "
                       "the frame-time window was NOT reset.\n");
  } else if (!result) {
    control_reply_text(fd, 409, "Conflict", "%s\n", reason);
  } else {
    control_reply_text(fd, 200, "OK", "%s\n", reason);
  }
}

/*
 * Arm (or disarm) the hot-guest-entry-point probe while the game runs.
 *
 * X2_HOTEP answers where an unattributed frame goes, but a released build has
 * no environment to set it in: the only way to measure a phone was to install
 * a debug build, which changes the signing identity and destroys the player's
 * imported game. The probe's arming is a single store the dispatch path reads,
 * so it is safe to flip from here, and the heartbeat prints the split on its
 * next interval. n=0 disarms and the probe goes back to answering nothing.
 */
void control_performance_probe_route(x2_socket_t fd, const char *query) {
  const char *n = strstr(query, "n=");
  unsigned long want;

  if (!n) {
    control_reply_text(fd, 400, "Bad Request",
                       "give the number of entry points to track: "
                       "/performance/probe?n=4096 (n=0 disarms)\n");
    return;
  }
  want = strtoul(n + 2, NULL, 10);
  x86_hotep_arm(n + 2);
  control_reply_text(fd, 200, "OK",
                     want ? "hot-entry-point probe armed for %lu entry "
                            "point(s); the next heartbeat prints the "
                            "wall-time split.\n"
                          : "hot-entry-point probe disarmed (%lu).\n",
                     want);
}
