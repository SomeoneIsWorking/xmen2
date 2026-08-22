#ifndef X2_INPUT_RECORD_H
#define X2_INPUT_RECORD_H

#include <stddef.h>
#include <stdint.h>

/*
 * Record the device snapshots DirectInput actually returned to the game.
 * An empty path selects a unique file below scratch/recordings/. NULL leaves
 * recording disabled. Every record is flushed immediately so a crashed run
 * still leaves the input that led to it.
 */
int input_record_start(const char *path);
void input_record_keyboard(const void *state, size_t bytes,
                           unsigned long frame, double guest_time_s);
void input_record_mouse(const void *state, size_t bytes,
                        unsigned long frame, double guest_time_s);
void input_record_gamepad(unsigned pad, const char *persistent_id,
                          const void *state, size_t bytes,
                          unsigned long frame, double guest_time_s);

const char *input_record_path(void);
unsigned long input_record_event_count(void);
void input_record_report(void);

#endif /* X2_INPUT_RECORD_H */
