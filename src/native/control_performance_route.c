#include "control_performance_route.h"

#include "control.h"
#include "control_command_bridge.h"

void control_performance_reset_route(int fd)
{
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
