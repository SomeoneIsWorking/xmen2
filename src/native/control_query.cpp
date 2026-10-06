#include "control_query.h"

#include <stdlib.h>
#include <string.h>

static int hex_digit(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  return -1;
}

/* Form decoding: '+' is a space and %XX a byte. A '%' not followed by two
   hex digits is kept as written. Stops at `out_size - 1` bytes. */
static void decode_value(const char *value, size_t size, char *out,
                         size_t out_size) {
  size_t in = 0, at = 0;
  while (in < size && at + 1 < out_size) {
    int high, low;
    if (value[in] == '+') {
      out[at++] = ' ';
      in++;
      continue;
    }
    if (value[in] == '%' && in + 2 < size &&
        (high = hex_digit(value[in + 1])) >= 0 &&
        (low = hex_digit(value[in + 2])) >= 0) {
      out[at++] = (char)(high * 16 + low);
      in += 3;
      continue;
    }
    out[at++] = value[in++];
  }
  out[at] = '\0';
}

int control_query_arg(const char *query, const char *name, char *out,
                      size_t out_size) {
  size_t name_size;
  const char *part;

  if (!query || !name || !out || !out_size)
    return 0;
  name_size = strlen(name);
  part = query;
  while (*part) {
    if (!strncmp(part, name, name_size) && part[name_size] == '=') {
      const char *value = part + name_size + 1;
      const char *end = strchr(value, '&');
      decode_value(value, end ? (size_t)(end - value) : strlen(value), out,
                   out_size);
      return 1;
    }
    part = strchr(part, '&');
    if (!part)
      break;
    part++;
  }
  return 0;
}

int bounded_number(const char *text, int minimum, int maximum, int *out) {
  char *end;
  long value;
  if (!text || !*text)
    return 0;
  value = strtol(text, &end, 10);
  if (*end || value < minimum || value > maximum)
    return 0;
  *out = (int)value;
  return 1;
}
