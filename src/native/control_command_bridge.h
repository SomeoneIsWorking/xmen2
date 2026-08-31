#ifndef X2_CONTROL_COMMAND_BRIDGE_H
#define X2_CONTROL_COMMAND_BRIDGE_H

#include <stddef.h>

/* Submit release-diagnostic commands through control's guest-thread queue.
 * A return of -1 is a timeout, 0 is a command refusal, and 1 is success. */
int control_command_save(const char **report, size_t *report_size,
                         char *reason, size_t reason_capacity);
int control_command_performance_reset(char *reason, size_t reason_capacity);

#endif
