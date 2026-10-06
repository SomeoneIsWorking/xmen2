#ifndef X2_INPUT_PROBE_LIFECYCLE_H
#define X2_INPUT_PROBE_LIFECYCLE_H

#include <stddef.h>

/* Render the host controller lifecycle and resolved player ownership into a
   bounded buffer. Returns the bytes present, excluding the terminator. */
size_t x2_input_probe_lifecycle_report(char *out, size_t n);

#endif /* X2_INPUT_PROBE_LIFECYCLE_H */
