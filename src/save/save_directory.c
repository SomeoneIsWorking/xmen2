#include "save_directory.h"

#include "shell32.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

int x2_retail_save_directory_from_root(const char *storage_root, char *out,
                                       size_t capacity) {
  const char *separator;
  size_t length;
  int result;

  if (!storage_root || !storage_root[0] || !out || !capacity) {
    errno = EINVAL;
    return 0;
  }
  length = strlen(storage_root);
  separator = storage_root[length - 1u] == '/' ? "" : "/";
  result = snprintf(out, capacity, "%s%s%s", storage_root, separator,
                    X2_RETAIL_SAVE_SUBDIRECTORY);
  if (result < 0 || (size_t)result >= capacity) {
    out[0] = 0;
    errno = ENAMETOOLONG;
    return 0;
  }
  return 1;
}

const char *x2_retail_save_directory(void) {
  static char directory[1200];
  static int ready;

  if (!ready) {
    ready = 1;
    if (!x2_retail_save_directory_from_root(x2_save_dir(), directory,
                                            sizeof directory))
      directory[0] = 0;
  }
  return directory[0] ? directory : NULL;
}
