/* Create a directory path, one component at a time. */
#include "directory_create.h"

#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>

int x2_directory_create(const char *path) {
  char work[512];
  char *cursor;

  if (!path || !path[0])
    return 0;
  if (snprintf(work, sizeof work, "%s", path) >= (int)sizeof work)
    return 0;
  for (cursor = work + 1; *cursor; cursor++) {
    if (*cursor != '/')
      continue;
    *cursor = '\0';
    if (mkdir(work, 0775) != 0 && errno != EEXIST)
      return 0;
    *cursor = '/';
  }
  return mkdir(work, 0775) == 0 || errno == EEXIST;
}
