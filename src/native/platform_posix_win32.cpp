/* platform_posix_win32.cpp -- the platform_posix.h calls UCRT lacks. */
#include "platform_posix.h"

#include <errno.h>
#include <io.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

extern "C" long x2_sysconf(int name) {
  SYSTEM_INFO info;
  GetSystemInfo(&info);
  if (name == _SC_PAGESIZE) {
    return static_cast<long>(info.dwPageSize);
  }
  if (name == _SC_NPROCESSORS_ONLN) {
    return static_cast<long>(info.dwNumberOfProcessors);
  }
  errno = EINVAL;
  return -1;
}

extern "C" int x2_replace_file(const char *from, const char *to) {
  if (MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING)) {
    return 0;
  }
  switch (GetLastError()) {
  case ERROR_FILE_NOT_FOUND:
  case ERROR_PATH_NOT_FOUND:
    errno = ENOENT;
    break;
  case ERROR_ACCESS_DENIED:
  case ERROR_SHARING_VIOLATION:
    errno = EACCES;
    break;
  default:
    errno = EIO;
    break;
  }
  return -1;
}

extern "C" ssize_t x2_pread(int descriptor, void *buffer, size_t size,
                            off_t offset) {
  const HANDLE file = reinterpret_cast<HANDLE>(_get_osfhandle(descriptor));
  if (file == INVALID_HANDLE_VALUE) {
    errno = EBADF;
    return -1;
  }
  const __int64 position = _lseeki64(descriptor, 0, SEEK_CUR);
  if (position < 0) {
    return -1;
  }
  OVERLAPPED at = {};
  const auto start = static_cast<unsigned long long>(offset);
  at.Offset = static_cast<DWORD>(start);
  at.OffsetHigh = static_cast<DWORD>(start >> 32);
  const DWORD request =
      size > 0x7fffffffu ? 0x7fffffffu : static_cast<DWORD>(size);
  DWORD transferred = 0;
  const BOOL read = ReadFile(file, buffer, request, &transferred, &at);
  const DWORD error = read ? ERROR_SUCCESS : GetLastError();
  // A synchronous handle moves its position even for an offset read.
  (void)_lseeki64(descriptor, position, SEEK_SET);
  if (!read && error != ERROR_HANDLE_EOF) {
    errno = EIO;
    return -1;
  }
  return static_cast<ssize_t>(transferred);
}
