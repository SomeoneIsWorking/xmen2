/*
 * Bounded report text: append formatted text to a fixed buffer.
 *
 * The probes answer over the control channel into a caller-sized buffer.
 * A report that outgrows it is truncated at the buffer's end, never
 * overrun, and `*at` never passes `size`, so every later append is a no-op.
 */
#ifndef X2_TEXT_PUT_H
#define X2_TEXT_PUT_H

#include <stddef.h>

void text_put(char *out, size_t size, size_t *at, const char *format, ...)
    __attribute__((format(printf, 4, 5)));

#endif
