#ifndef X2_CONTROL_QUERY_H
#define X2_CONTROL_QUERY_H

#include <stddef.h>

/* Copies one value from an already-separated HTTP query string, form-decoded
   ('+' and %XX), so a key named "Keypad 6" can be asked for. Truncated to
   fit `out`. Returns 0 when the query has no such name. */
int control_query_arg(const char *query, const char *name, char *out,
                      size_t out_size);

/* Parses one bounded decimal integer; 0 and *out untouched on any refusal. */
int bounded_number(const char *text, int minimum, int maximum, int *out);

#endif /* X2_CONTROL_QUERY_H */
