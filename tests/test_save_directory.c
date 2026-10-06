#include "save_directory.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

static int checks;
#define CHECK(c)                                                               \
  do {                                                                         \
    assert(c);                                                                 \
    checks++;                                                                  \
  } while (0)

const char *x2_save_dir(void) { return "scratch/profile-root"; }

int main(void) {
  char path[256];
  char short_path[8] = "stale";

  CHECK(
      x2_retail_save_directory_from_root("scratch/profile", path, sizeof path));
  CHECK(!strcmp(path, "scratch/profile/" X2_RETAIL_SAVE_SUBDIRECTORY));
  CHECK(x2_retail_save_directory_from_root("scratch/profile/", path,
                                           sizeof path));
  CHECK(!strcmp(path, "scratch/profile/" X2_RETAIL_SAVE_SUBDIRECTORY));
  CHECK(!x2_retail_save_directory_from_root(NULL, path, sizeof path));
  CHECK(errno == EINVAL);
  CHECK(!x2_retail_save_directory_from_root("", path, sizeof path));
  CHECK(errno == EINVAL);
  CHECK(!x2_retail_save_directory_from_root("scratch/profile", short_path,
                                            sizeof short_path));
  CHECK(errno == ENAMETOOLONG && short_path[0] == 0);
  CHECK(!strcmp(x2_retail_save_directory(),
                "scratch/profile-root/" X2_RETAIL_SAVE_SUBDIRECTORY));

  printf("save_directory: %d checks passed\n", checks);
  return 0;
}
