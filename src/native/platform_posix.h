#ifndef X2_PLATFORM_POSIX_H
#define X2_PLATFORM_POSIX_H

/* POSIX file and process calls. MinGW-w64's unistd.h covers most of them on
 * Windows; the rest map to their UCRT equivalents here. */
#include <unistd.h>

#if defined(_WIN32)

#include <direct.h>
#include <fcntl.h>
#include <io.h>
#include <stdlib.h>
#include <sys/stat.h>

#ifndef O_DIRECTORY
#define O_DIRECTORY 0
#endif
#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif
#ifndef O_NONBLOCK
#define O_NONBLOCK 0
#endif
/* A CRT descriptor is never a socket on Windows. */
#ifndef S_ISSOCK
#define S_ISSOCK(mode) 0
#endif
/* NTFS's component limit. */
#ifndef NAME_MAX
#define NAME_MAX 255
#endif
#ifndef _SC_PAGESIZE
#define _SC_PAGESIZE 1
#endif
#ifndef _SC_NPROCESSORS_ONLN
#define _SC_NPROCESSORS_ONLN 2
#endif

#ifdef __cplusplus
extern "C" {
#endif
/* platform_posix_win32.cpp */
long x2_sysconf(int name);
/* Reads at an offset and leaves the descriptor's position where it was. */
ssize_t x2_pread(int descriptor, void *buffer, size_t size, off_t offset);
/* rename(2)'s contract: an existing `to` is replaced in one step. */
int x2_replace_file(const char *from, const char *to);
#ifdef __cplusplus
}
#endif

static inline int x2_mkdir(const char *path, int mode) {
  (void)mode;
  return _mkdir(path);
}
/* Host paths come back with '/' separators, as on POSIX; Windows accepts
   either, so callers split and join them one way. */
static inline char *x2_forward_slashes(char *path) {
  for (char *at = path; at && *at; ++at) {
    if (*at == '\\')
      *at = '/';
  }
  return path;
}
static inline char *x2_realpath(const char *path, char *resolved) {
  return x2_forward_slashes(_fullpath(resolved, path, _MAX_PATH));
}
static inline char *x2_getcwd(char *buffer, size_t size) {
  return x2_forward_slashes(_getcwd(buffer, (int)size));
}
static inline int x2_pipe(int descriptors[2]) {
  return _pipe(descriptors, 4096, _O_BINARY);
}
static inline int x2_fsync(int descriptor) { return _commit(descriptor); }

#define fsync x2_fsync
#define getcwd x2_getcwd
#define mkdir x2_mkdir
#define pipe x2_pipe
#define pread x2_pread
#define realpath x2_realpath
#define sysconf x2_sysconf

#else

#include <stdio.h>

static inline int x2_replace_file(const char *from, const char *to) {
  return rename(from, to);
}

#endif

#endif
