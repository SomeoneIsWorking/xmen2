#include "../config/environment.h"
#include "../native/x2_log.h"
/*
 * X2_LIGHTLOG=<path> writes the same light-state stream as the forwarding
 * D3D8 DLL used by the stock Wine control. A byte-comparable format turns
 * "did the original engine do the same?" into a diff rather than an argument.
 */
#include "d3d8_lightlog.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static FILE *g_log;
static int g_tried;
static long g_t0;

long d3d8_lightlog_ms(void) {
  struct timespec ts;
  long ms;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  ms = (long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
  if (!g_t0)
    g_t0 = ms;
  return ms - g_t0;
}

static int lightlog_on(void) {
  const char *path;
  if (g_tried)
    return g_log != NULL;
  g_tried = 1;
  path = x2_config_override_get(kX2ConfigLightLog);
  if (!path || !*path)
    return 0;
  g_log = fopen(path, "w");
  if (!g_log) {
    x2_log_error("d3d8: X2_LIGHTLOG=%s could NOT be opened -- no light "
                 "log will be written by this run.\n",
                 path);
    return 0;
  }
  setvbuf(g_log, NULL, _IOLBF, 8192); /* a kill keeps the tail */
  x2_log_error("d3d8: X2_LIGHTLOG is writing %s\n", path);
  return 1;
}

void d3d8_lightlog(const char *fmt, ...) {
  va_list ap;
  if (!lightlog_on())
    return;
  va_start(ap, fmt);
  vfprintf(g_log, fmt, ap);
  va_end(ap);
  fputc('\n', g_log);
}
