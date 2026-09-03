#ifndef X2_CONTROL_QUERY_H
#define X2_CONTROL_QUERY_H

#include <stddef.h>

/* Copies one unescaped value from an already-separated HTTP query string.
   The control protocol deliberately accepts only the numeric/identifier
   vocabulary used by its loopback diagnostic routes. */
int control_query_arg(const char *query, const char *name, char *out,
                      size_t out_size);

/* Parses one bounded decimal integer; 0 and *out untouched on any refusal. */
int bounded_number(const char *text, int minimum, int maximum, int *out);

#endif /* X2_CONTROL_QUERY_H */
