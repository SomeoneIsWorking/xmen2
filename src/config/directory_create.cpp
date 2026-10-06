/* Create a directory path, one component at a time. */
#include "directory_create.h"

#include "platform_posix.h"

#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>

namespace {

bool is_separator(char c) {
#if defined(_WIN32)
  return c == '/' || c == '\\';
#else
  return c == '/';
#endif
}

// The first character that can end a component: past a leading separator,
// and on Windows past a drive ("C:").
char *first_component(char *work) {
#if defined(_WIN32)
  if (work[0] && work[1] == ':') {
    work += 2;
  }
#endif
  return is_separator(work[0]) ? work + 1 : work;
}

} // namespace

int x2_directory_create(const char *path) {
  char work[512];

  if (!path || !path[0]) {
    return 0;
  }
  if (snprintf(work, sizeof work, "%s", path) >= (int)sizeof work) {
    return 0;
  }
  for (char *cursor = first_component(work); *cursor; cursor++) {
    if (!is_separator(*cursor)) {
      continue;
    }
    const char separator = *cursor;
    *cursor = '\0';
    if (mkdir(work, 0775) != 0 && errno != EEXIST) {
      return 0;
    }
    *cursor = separator;
  }
  return mkdir(work, 0775) == 0 || errno == EEXIST;
}
