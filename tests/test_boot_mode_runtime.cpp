#include "boot_mode_runtime.h"
#include "save_directory.h"

#include "platform_posix.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static int checks;
#define CHECK(c)                                                               \
  do {                                                                         \
    assert(c);                                                                 \
    checks++;                                                                  \
  } while (0)

namespace x2::native {
const char *save_dir(void) { return X2_TEST_BOOT_STORAGE_ROOT; }
} // namespace x2::native

static void ensure_directory(const char *path) {
  CHECK(mkdir(path, 0700) == 0 || errno == EEXIST);
}

int main(void) {
  const char *directory;
  char path[1024];
  char save_path[1024];
  const x2::native::BootModeDecision *decision;
  FILE *save;

  ensure_directory(X2_TEST_BOOT_STORAGE_ROOT);
  snprintf(path, sizeof path, "%s/Activision", X2_TEST_BOOT_STORAGE_ROOT);
  ensure_directory(path);
  snprintf(path, sizeof path, "%s/Activision/X-Men Legends 2",
           X2_TEST_BOOT_STORAGE_ROOT);
  ensure_directory(path);
  directory = x2::save::retail_save_directory();
  CHECK(directory != NULL);
  ensure_directory(directory);
  snprintf(save_path, sizeof save_path, "%s/saveslot2.save", directory);
  CHECK(remove(save_path) == 0 || errno == ENOENT);
  save = fopen(save_path, "wb");
  CHECK(save != NULL);
  CHECK(fputs("opaque", save) >= 0);
  CHECK(fclose(save) == 0);

  decision = x2::native::boot_mode_runtime_prepare(
      x2::config::BootMode::Continue, directory);
  CHECK(decision->requested == x2::config::BootMode::Continue);
  CHECK(decision->effective == x2::config::BootMode::Continue);
  CHECK(!decision->fell_back_to_menu);
  CHECK(!x2::native::boot_mode_runtime_catalog_failed());
  CHECK(x2::native::boot_mode_runtime_continue_leaf() != NULL);
  CHECK(strcmp(x2::native::boot_mode_runtime_continue_leaf(),
               "saveslot2.save") == 0);
  x2::native::boot_mode_runtime_continue_started();
  CHECK(x2::native::boot_mode_runtime_continue_leaf() == NULL);

  CHECK(remove(save_path) == 0);
  CHECK(rmdir(directory) == 0);
  snprintf(path, sizeof path, "%s/Activision/X-Men Legends 2",
           X2_TEST_BOOT_STORAGE_ROOT);
  CHECK(rmdir(path) == 0);
  snprintf(path, sizeof path, "%s/Activision", X2_TEST_BOOT_STORAGE_ROOT);
  CHECK(rmdir(path) == 0);
  CHECK(rmdir(X2_TEST_BOOT_STORAGE_ROOT) == 0);
  printf("boot_mode_runtime: %d checks passed\n", checks);
  return 0;
}
