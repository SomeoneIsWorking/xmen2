#ifndef X2_CONTROL_COMMAND_BRIDGE_H
#define X2_CONTROL_COMMAND_BRIDGE_H

#include <stddef.h>

/* Submit release-diagnostic commands through control's guest-thread queue.
 * A return of -1 is a timeout, 0 is a command refusal, and 1 is success. */
int control_command_save(const char **report, size_t *report_size, char *reason,
                         size_t reason_capacity);
int control_command_performance_reset(char *reason, size_t reason_capacity);

/* Drive the game's input. Each of these runs on the thread that owns guest
   input, for the same reason: the guest is single-threaded under a
   cooperative scheduler, so the server thread must never reach into it. The
   reason buffer carries what the command itself said, including the pad's own
   read-back of a press. */
int control_command_key(const char *name, double hold, char *reason,
                        size_t reason_capacity);
int control_command_pad(const char *what, double value, double hold,
                        char *reason, size_t reason_capacity);
int control_command_touch(double x, double y, int phase, char *reason,
                          size_t reason_capacity);
/* `pad_or_clear` is the live pad index, or -1 to remove the eligibility. */
int control_command_assignment(unsigned player_index, double pad_or_clear,
                               char *reason, size_t reason_capacity);

#endif
