#include "x2_log.h"

#include "crt_console.h"

#include <string.h>

/* One line at a time. Longer than this is logged in pieces rather than
   truncated: the guest's own messages are short, and a message that is cut
   off is worse than one that wraps. */
#define LINE_MAX 1024
static char g_line[LINE_MAX];
static size_t g_len;

int crt_console_is(FILE *f) { return f == stdout || f == stderr; }

static void emit(void) {
  if (!g_len) {
    return;
  }
  g_line[g_len] = '\0';
  x2_log_info("[guest] %s\n", g_line);
  g_len = 0;
}

size_t crt_console_write(const char *bytes, size_t n) {
  size_t i;
  for (i = 0; i < n; i++) {
    char c = bytes[i];
    if (c == '\n') {
      emit();
      continue;
    }
    if (c == '\r') {
      continue;
    }
    if (g_len + 1u >= LINE_MAX) {
      emit();
    }
    g_line[g_len++] = c;
  }
  return n;
}

void crt_console_flush(void) { emit(); }
