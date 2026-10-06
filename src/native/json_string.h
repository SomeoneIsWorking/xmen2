#ifndef X2_JSON_STRING_H
#define X2_JSON_STRING_H

#include <stddef.h>

/* Format one complete JSON string value, including its quotes. */
int json_string_format(char *out, size_t capacity, const char *value);

#endif /* X2_JSON_STRING_H */
