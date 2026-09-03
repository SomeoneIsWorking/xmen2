#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#else
#define _POSIX_C_SOURCE 200809L
#endif

#include "autosave_storage.h"
#include "save_directory.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int checks;
static int failures;
static char storage_root[] = "scratch/autosave-storage-XXXXXX";
#define CHECK(x)                                                               \
  do {                                                                         \
    checks++;                                                                  \
    if (!(x)) {                                                                \
      failures++;                                                              \
      fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #x);  \
    }                                                                          \
  } while (0)

const char *x2_save_dir(void) { return storage_root; }

static void ensure_directory(const char *path) {
  CHECK(mkdir(path, 0700) == 0 || errno == EEXIST);
}

static int write_file(const char *path, const void *data, size_t size) {
  int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
  ssize_t result = fd < 0 ? -1 : write(fd, data, size);
  if (fd >= 0)
    close(fd);
  return result == (ssize_t)size;
}

static size_t read_file(const char *path, unsigned char *out, size_t capacity) {
  int fd = open(path, O_RDONLY);
  ssize_t result = fd < 0 ? -1 : read(fd, out, capacity);
  if (fd >= 0)
    close(fd);
  return result < 0 ? 0u : (size_t)result;
}

static int temporary_count(const char *directory) {
  DIR *dir = opendir(directory);
  struct dirent *entry;
  int count = 0;
  if (!dir)
    return -1;
  while ((entry = readdir(dir)) != NULL)
    if (!strncmp(entry->d_name, ".autosave.save.tmp.", 19u))
      count++;
  closedir(dir);
  return count;
}

int main(void) {
  unsigned char header[X2_SAVE_HEADER_BYTES];
  const unsigned char payload[] = {1, 2, 3, 4, 5};
  const unsigned char prior[] = "prior-autosave";
  unsigned char result[256];
  const char *directory;
  char parent[256];
  char leaf[256];
  X2AutosaveStorageFault fault;
  size_t size;
  unsigned i;

  CHECK(mkdtemp(storage_root) != NULL);
  snprintf(parent, sizeof parent, "%s/Activision", storage_root);
  ensure_directory(parent);
  snprintf(parent, sizeof parent, "%s/Activision/X-Men Legends 2",
           storage_root);
  ensure_directory(parent);
  directory = x2_retail_save_directory();
  CHECK(directory != NULL);
  ensure_directory(directory);
  snprintf(leaf, sizeof leaf, "%s/%s", directory, X2_AUTOSAVE_LEAF);
  memset(header, 0xa5, sizeof header);

  for (fault = X2_AUTOSAVE_FAULT_AFTER_HEADER;
       fault <= X2_AUTOSAVE_FAULT_BEFORE_RENAME; fault++) {
    CHECK(write_file(leaf, prior, sizeof prior));
    CHECK(!x2_autosave_storage_publish(directory, header, payload,
                                       sizeof payload, fault));
    size = read_file(leaf, result, sizeof result);
    CHECK(size == sizeof prior);
    CHECK(!memcmp(result, prior, sizeof prior));
    CHECK(temporary_count(directory) == 0);
  }

  CHECK(x2_autosave_storage_publish(directory, header, payload, sizeof payload,
                                    X2_AUTOSAVE_FAULT_NONE));
  size = read_file(leaf, result, sizeof result);
  CHECK(size == X2_SAVE_HEADER_BYTES + 4u + sizeof payload);
  CHECK(!memcmp(result, header, sizeof header));
  CHECK(result[128] == sizeof payload && result[129] == 0 && result[130] == 0 &&
        result[131] == 0);
  CHECK(!memcmp(result + 132u, payload, sizeof payload));
  CHECK(temporary_count(directory) == 0);

  CHECK(!x2_autosave_storage_publish(NULL, header, payload, sizeof payload,
                                     X2_AUTOSAVE_FAULT_NONE));
  for (i = 0; i < sizeof result; i++)
    result[i] = 0;
  CHECK(unlink(leaf) == 0);
  CHECK(rmdir(directory) == 0);
  snprintf(parent, sizeof parent, "%s/Activision/X-Men Legends 2",
           storage_root);
  CHECK(rmdir(parent) == 0);
  snprintf(parent, sizeof parent, "%s/Activision", storage_root);
  CHECK(rmdir(parent) == 0);
  CHECK(rmdir(storage_root) == 0);

  printf("autosave_storage: %d checks, %d failures\n", checks, failures);
  return failures != 0;
}
