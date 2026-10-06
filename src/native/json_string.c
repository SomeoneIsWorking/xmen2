#include "json_string.h"

#include <stdio.h>

static int append(char *out, size_t capacity, size_t *used, const char *text,
                  size_t bytes) {
  size_t i;
  if (*used + bytes >= capacity)
    return 0;
  for (i = 0; i < bytes; i++)
    out[(*used)++] = text[i];
  out[*used] = '\0';
  return 1;
}

int json_string_format(char *out, size_t capacity, const char *value) {
  const unsigned char *p = (const unsigned char *)(value ? value : "");
  size_t used = 0;

  if (!out || capacity < 3u || !append(out, capacity, &used, "\"", 1u))
    return 0;
  for (; *p; p++) {
    char escaped[7];
    if (*p == '"' || *p == '\\') {
      escaped[0] = '\\';
      escaped[1] = (char)*p;
      if (!append(out, capacity, &used, escaped, 2u))
        return 0;
    } else if (*p < 0x20u) {
      snprintf(escaped, sizeof escaped, "\\u%04x", *p);
      if (!append(out, capacity, &used, escaped, 6u))
        return 0;
    } else if (!append(out, capacity, &used, (const char *)p, 1u)) {
      return 0;
    }
  }
  return append(out, capacity, &used, "\"", 1u);
}
