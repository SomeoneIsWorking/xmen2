#include "save_directory.h"

#include "shell32.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

namespace x2::save {

int retail_save_directory_from_root(const char *storage_root, char *out,
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
                    kRetailSaveSubdirectory);
  if (result < 0 || (size_t)result >= capacity) {
    out[0] = 0;
    errno = ENAMETOOLONG;
    return 0;
  }
  return 1;
}

const char *retail_save_directory(void) {
  static char directory[1200];
  static int ready;

  if (!ready) {
    ready = 1;
    if (!retail_save_directory_from_root(x2::native::save_dir(), directory,
                                         sizeof directory))
      directory[0] = 0;
  }
  return directory[0] ? directory : NULL;
}

} // namespace x2::save
