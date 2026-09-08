#ifndef X2_PLATFORM_POSIX_H
#define X2_PLATFORM_POSIX_H

#if defined(_WIN32)

#include <direct.h>
#include <fcntl.h>
#include <io.h>
#include <process.h>
#include <sys/stat.h>
#include <windows.h>

#include <errno.h>
#include <stddef.h>
#include <stdint.h>

#ifndef _OFF_T_DEFINED
typedef __int64 off_t;
#define _OFF_T_DEFINED
#endif
typedef SSIZE_T ssize_t;

#ifndef STDIN_FILENO
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#endif
#ifndef F_OK
#define F_OK 0
#endif
#ifndef O_DIRECTORY
#define O_DIRECTORY 0
#endif
#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif
#ifndef O_NONBLOCK
#define O_NONBLOCK 0
#endif
#ifndef _SC_PAGESIZE
#define _SC_PAGESIZE 1
#endif
#ifndef _SC_NPROCESSORS_ONLN
#define _SC_NPROCESSORS_ONLN 2
#endif

static inline int x2_mkdir(const char *path, int mode) {
  (void)mode;
  return _mkdir(path);
}
static inline char *x2_realpath(const char *path, char *resolved) {
  return _fullpath(resolved, path, _MAX_PATH);
}
static inline long x2_sysconf(int name) {
  SYSTEM_INFO info;
  if (name == _SC_PAGESIZE) {
    GetSystemInfo(&info);
    return (long)info.dwPageSize;
  }
  if (name == _SC_NPROCESSORS_ONLN) {
    GetSystemInfo(&info);
    return (long)info.dwNumberOfProcessors;
  }
  errno = EINVAL;
  return -1;
}
static inline int x2_pipe(int descriptors[2]) {
  return _pipe(descriptors, 4096, _O_BINARY);
}
static inline int x2_ftruncate(int descriptor, off_t size) {
  return _chsize_s(descriptor, (size_t)size) == 0 ? 0 : -1;
}
static inline int x2_fsync(int descriptor) { return _commit(descriptor); }
static inline off_t x2_lseek(int descriptor, off_t offset, int whence) {
  return (off_t)_lseeki64(descriptor, offset, whence);
}

#define access _access
#define close _close
#define dup _dup
#define dup2 _dup2
#define fsync x2_fsync
#define ftruncate x2_ftruncate
#define getpid _getpid
#define lseek x2_lseek
#define mkdir x2_mkdir
#define pipe x2_pipe
#define read _read
#define realpath x2_realpath
#define sysconf x2_sysconf
#define unlink _unlink
#define write _write

#else

#include <unistd.h>

#endif

#endif
