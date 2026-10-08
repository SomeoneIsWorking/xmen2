#include "save_directory.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <string>

static int checks;
#define CHECK(c)                                                               \
  do {                                                                         \
    assert(c);                                                                 \
    checks++;                                                                  \
  } while (0)

namespace x2::native {
const char *save_dir(void) { return "scratch/profile-root"; }
} // namespace x2::native

int main(void) {
  char path[256];
  char short_path[8] = "stale";

  CHECK(x2::save::retail_save_directory_from_root("scratch/profile", path,
                                                  sizeof path));
  CHECK(!strcmp(path, (std::string("scratch/profile/") +
                       x2::save::kRetailSaveSubdirectory)
                          .c_str()));
  CHECK(x2::save::retail_save_directory_from_root("scratch/profile/", path,
                                                  sizeof path));
  CHECK(!strcmp(path, (std::string("scratch/profile/") +
                       x2::save::kRetailSaveSubdirectory)
                          .c_str()));
  CHECK(!x2::save::retail_save_directory_from_root(NULL, path, sizeof path));
  CHECK(errno == EINVAL);
  CHECK(!x2::save::retail_save_directory_from_root("", path, sizeof path));
  CHECK(errno == EINVAL);
  CHECK(!x2::save::retail_save_directory_from_root(
      "scratch/profile", short_path, sizeof short_path));
  CHECK(errno == ENAMETOOLONG && short_path[0] == 0);
  CHECK(!strcmp(
      x2::save::retail_save_directory(),
      (std::string("scratch/profile-root/") + x2::save::kRetailSaveSubdirectory)
          .c_str()));

  printf("save_directory: %d checks passed\n", checks);
  return 0;
}
