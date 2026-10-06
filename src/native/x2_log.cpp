#include "x2_log.h"

#include <lucent/log_c.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

static void x2_log_message(LucentLogLevel level, const char *format,
                           va_list arguments) {
  va_list sizing;
  char *message;
  int length;

  va_copy(sizing, arguments);
  length = vsnprintf(NULL, 0, format, sizing);
  va_end(sizing);
  if (length < 0) {
    lucent_log_error("x2", "diagnostic formatting failed");
    return;
  }
  message = malloc((size_t)length + 1u);
  if (!message) {
    lucent_log_error("x2", "diagnostic allocation failed (%d bytes)",
                     length + 1);
    return;
  }
  (void)vsnprintf(message, (size_t)length + 1u, format, arguments);
  for (int index = 0; index < length; ++index) {
    if (message[index] == '\n' || message[index] == '\r')
      message[index] = ' ';
  }
  while (length > 0 && message[length - 1] == ' ')
    message[--length] = '\0';
  lucent_log(level, "x2", "%s", message);
  free(message);
}

void x2_log_error(const char *format, ...) {
  va_list arguments;

  va_start(arguments, format);
  x2_log_message(LUCENT_LOG_ERROR, format, arguments);
  va_end(arguments);
}

void x2_log_info(const char *format, ...) {
  va_list arguments;

  va_start(arguments, format);
  x2_log_message(LUCENT_LOG_INFO, format, arguments);
  va_end(arguments);
}
