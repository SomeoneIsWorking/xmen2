#include "text_put.h"

#include <stdarg.h>
#include <stdio.h>

void text_put(char *out, size_t size, size_t *at, const char *format, ...) {
  va_list args;
  int written;
  if (*at >= size)
    return;
  va_start(args, format);
  written = vsnprintf(out + *at, size - *at, format, args);
  va_end(args);
  if (written > 0)
    *at += (size_t)written < size - *at ? (size_t)written : size - *at;
}
