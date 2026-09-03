#include "boot_mode_runtime.h"
#include "save_directory.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int checks;
#define CHECK(c)                                                               \
  do {                                                                         \
    assert(c);                                                                 \
    checks++;                                                                  \
  } while (0)

const char *x2_save_dir(void) { return X2_TEST_BOOT_STORAGE_ROOT; }

static void ensure_directory(const char *path) {
  CHECK(mkdir(path, 0700) == 0 || errno == EEXIST);
}

int main(void) {
  const char *directory;
  char path[1024];
  char save_path[1024];
  const X2BootModeDecision *decision;
  FILE *save;

  ensure_directory(X2_TEST_BOOT_STORAGE_ROOT);
  snprintf(path, sizeof path, "%s/Activision", X2_TEST_BOOT_STORAGE_ROOT);
  ensure_directory(path);
  snprintf(path, sizeof path, "%s/Activision/X-Men Legends 2",
           X2_TEST_BOOT_STORAGE_ROOT);
  ensure_directory(path);
  directory = x2_retail_save_directory();
  CHECK(directory != NULL);
  ensure_directory(directory);
  snprintf(save_path, sizeof save_path, "%s/saveslot2.save", directory);
  CHECK(remove(save_path) == 0 || errno == ENOENT);
  save = fopen(save_path, "wb");
  CHECK(save != NULL);
  CHECK(fputs("opaque", save) >= 0);
  CHECK(fclose(save) == 0);

  decision = x2_boot_mode_runtime_prepare(X2_BOOT_CONTINUE, directory);
  CHECK(decision->requested == X2_BOOT_CONTINUE);
  CHECK(decision->effective == X2_BOOT_CONTINUE);
  CHECK(!decision->fell_back_to_menu);
  CHECK(!x2_boot_mode_runtime_catalog_failed());
  CHECK(x2_boot_mode_runtime_continue_leaf() != NULL);
  CHECK(strcmp(x2_boot_mode_runtime_continue_leaf(), "saveslot2.save") == 0);
  x2_boot_mode_runtime_continue_started();
  CHECK(x2_boot_mode_runtime_continue_leaf() == NULL);

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
