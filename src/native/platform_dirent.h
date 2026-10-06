#ifndef X2_PLATFORM_DIRENT_H
#define X2_PLATFORM_DIRENT_H

#if defined(_WIN32)

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <windows.h>

#ifndef S_ISREG
#define S_ISREG(m) (((m) & _S_IFMT) == _S_IFREG)
#endif
#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
#endif

struct dirent {
  char d_name[MAX_PATH];
};

typedef struct DIR {
  HANDLE handle;
  WIN32_FIND_DATAA find_data;
  struct dirent entry;
  int first;
  char pattern[MAX_PATH];
} DIR;

static inline DIR *opendir(const char *name) {
  DIR *dir;
  size_t len;
  DWORD err;

  if (!name || !*name) {
    errno = ENOENT;
    return NULL;
  }
  dir = (DIR *)calloc(1, sizeof(DIR));
  if (!dir) {
    errno = ENOMEM;
    return NULL;
  }
  len = strlen(name);
  if (len + 3 >= MAX_PATH) {
    free(dir);
    errno = ENAMETOOLONG;
    return NULL;
  }
  memcpy(dir->pattern, name, len);
  if (name[len - 1] != '/' && name[len - 1] != '\\')
    dir->pattern[len++] = '/';
  dir->pattern[len++] = '*';
  dir->pattern[len] = '\0';
  dir->handle = FindFirstFileA(dir->pattern, &dir->find_data);
  if (dir->handle == INVALID_HANDLE_VALUE) {
    err = GetLastError();
    free(dir);
    errno = (err == ERROR_PATH_NOT_FOUND || err == ERROR_FILE_NOT_FOUND)
                ? ENOENT
                : EACCES;
    return NULL;
  }
  dir->first = 1;
  return dir;
}

static inline struct dirent *readdir(DIR *dir) {
  if (!dir || dir->handle == INVALID_HANDLE_VALUE)
    return NULL;
  if (dir->first) {
    dir->first = 0;
  } else {
    if (!FindNextFileA(dir->handle, &dir->find_data))
      return NULL;
  }
  strncpy(dir->entry.d_name, dir->find_data.cFileName,
          sizeof(dir->entry.d_name) - 1);
  dir->entry.d_name[sizeof(dir->entry.d_name) - 1] = '\0';
  return &dir->entry;
}

static inline int closedir(DIR *dir) {
  if (!dir) {
    errno = EBADF;
    return -1;
  }
  if (dir->handle != INVALID_HANDLE_VALUE)
    FindClose(dir->handle);
  free(dir);
  return 0;
}

static inline void rewinddir(DIR *dir) {
  if (!dir)
    return;
  if (dir->handle != INVALID_HANDLE_VALUE) {
    FindClose(dir->handle);
    dir->handle = FindFirstFileA(dir->pattern, &dir->find_data);
    dir->first = (dir->handle != INVALID_HANDLE_VALUE) ? 1 : 0;
  }
}

#else

#include <dirent.h>

#endif

#endif /* X2_PLATFORM_DIRENT_H */
