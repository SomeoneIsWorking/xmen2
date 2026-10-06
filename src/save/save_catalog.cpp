#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#else
#define _POSIX_C_SOURCE 200809L
#endif

#include "save_catalog.h"

#include "platform_dirent.h"
#include "platform_posix.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#define NS_PER_SECOND INT64_C(1000000000)

static int is_save_leaf(const char *leaf) {
  if (!strcmp(leaf, "autosave.save"))
    return 1;
  return !strncmp(leaf, "saveslot", 8) && leaf[8] >= '0' && leaf[8] <= '9' &&
         !strcmp(leaf + 9, ".save");
}

#if defined(_WIN32)
/* FILETIME counts 100 ns ticks from 1601; this many lie before 1970. */
#define FILETIME_UNIX_EPOCH INT64_C(116444736000000000)

/* A regular file's write time, as fstatat with AT_SYMLINK_NOFOLLOW sees it:
   0 with *regular clear for a directory or reparse point. */
static int windows_entry(const char *path, int *regular, int64_t *out) {
  WIN32_FILE_ATTRIBUTE_DATA data;
  if (!GetFileAttributesExA(path, GetFileExInfoStandard, &data)) {
    errno = GetLastError() == ERROR_FILE_NOT_FOUND ? ENOENT : EIO;
    return 0;
  }
  *regular = !(data.dwFileAttributes &
               (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT));
  const int64_t ticks =
      (int64_t)(((uint64_t)data.ftLastWriteTime.dwHighDateTime << 32) |
                data.ftLastWriteTime.dwLowDateTime);
  *out = (ticks - FILETIME_UNIX_EPOCH) * 100;
  return 1;
}
#else
static int mtime_ns(const struct stat *st, int64_t *out) {
#if defined(__APPLE__)
  int64_t seconds = (int64_t)st->st_mtimespec.tv_sec;
  int64_t nanoseconds = (int64_t)st->st_mtimespec.tv_nsec;
#else
  int64_t seconds = (int64_t)st->st_mtim.tv_sec;
  int64_t nanoseconds = (int64_t)st->st_mtim.tv_nsec;
#endif
  int64_t base;

  if (seconds > INT64_MAX / NS_PER_SECOND ||
      seconds < INT64_MIN / NS_PER_SECOND || nanoseconds < 0 ||
      nanoseconds >= NS_PER_SECOND) {
    errno = EOVERFLOW;
    return 0;
  }
  base = seconds * NS_PER_SECOND;
  if (base > INT64_MAX - nanoseconds) {
    errno = EOVERFLOW;
    return 0;
  }
  *out = base + nanoseconds;
  return 1;
}
#endif

static int is_newer(const X2SaveCandidate *candidate,
                    const X2SaveCandidate *current) {
  if (candidate->mtime_ns != current->mtime_ns)
    return candidate->mtime_ns > current->mtime_ns;
  return strcmp(candidate->leaf, current->leaf) > 0;
}

int x2_save_catalog_latest(const char *directory, X2SaveCandidate *out) {
  X2SaveCandidate candidate;
  struct dirent *entry;
  DIR *dir;
  int directory_fd;
  int found = 0;
  int result = 0;

  if (!directory || !directory[0] || !out) {
    errno = EINVAL;
    return -1;
  }
  dir = opendir(directory);
  if (!dir)
    return errno == ENOENT ? 0 : -1;
#if !defined(_WIN32)
  directory_fd = dirfd(dir);
  if (directory_fd < 0) {
    closedir(dir);
    return -1;
  }
#else
  (void)directory_fd;
#endif

  errno = 0;
  while ((entry = readdir(dir)) != NULL) {
    if (!is_save_leaf(entry->d_name))
      continue;
#if defined(_WIN32)
    char filepath[PATH_MAX];
    int regular = 0;
    int written =
        snprintf(filepath, sizeof filepath, "%s/%s", directory, entry->d_name);
    if (written <= 0 || (size_t)written >= sizeof filepath ||
        !windows_entry(filepath, &regular, &candidate.mtime_ns)) {
      result = -1;
      break;
    }
    if (!regular)
      continue;
#else
    struct stat st;
    if (fstatat(directory_fd, entry->d_name, &st, AT_SYMLINK_NOFOLLOW) != 0) {
      result = -1;
      break;
    }
    if (!S_ISREG(st.st_mode))
      continue;
    if (!mtime_ns(&st, &candidate.mtime_ns)) {
      result = -1;
      break;
    }
#endif
    memcpy(candidate.leaf, entry->d_name, strlen(entry->d_name) + 1);
    if (!found || is_newer(&candidate, out)) {
      *out = candidate;
      found = 1;
    }
    errno = 0;
  }
  if (result == 0 && errno != 0)
    result = -1;
  if (closedir(dir) != 0 && result == 0)
    result = -1;
  if (result != 0)
    return result;
  return found;
}
