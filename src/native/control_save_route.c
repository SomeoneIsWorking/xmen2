#include "control_save_route.h"

#include "control.h"
#include "control_command_bridge.h"

void control_save_route(int fd) {
  char reason[192];
  const char *report;
  size_t report_size;
  const int result =
      control_command_save(&report, &report_size, reason, sizeof reason);
  if (result < 0) {
    control_reply_text(
        fd, 504, "Gateway Timeout",
        "the guest did not reach an input poll within 10s, so the "
        "save trace was not read. The run is stuck, still loading, "
        "or has not reached its input loop.\n");
  } else if (!result) {
    control_reply_text(fd, 409, "Conflict", "%s\n", reason);
  } else {
    control_reply_text(fd, 200, "OK", "%.*s", (int)report_size, report);
  }
}
