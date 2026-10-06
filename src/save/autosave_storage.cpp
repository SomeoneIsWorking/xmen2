#define _POSIX_C_SOURCE 200809L

#include "autosave_storage.h"

#include "platform_posix.h"
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

static int write_complete(int fd, const void *data, size_t size) {
  const unsigned char *bytes = static_cast<const unsigned char *>(data);
  while (size) {
    ssize_t written = write(fd, bytes, size);
    if (written < 0 && errno == EINTR)
      continue;
    if (written <= 0)
      return 0;
    bytes += (size_t)written;
    size -= (size_t)written;
  }
  return 1;
}

static int injected(X2AutosaveStorageFault requested,
                    X2AutosaveStorageFault point) {
  if (requested != point)
    return 0;
  errno = EIO;
  return 1;
}

/* Header, little-endian payload length, payload, then a file sync. */
static int write_record(int file_fd, const void *header, const void *payload,
                        size_t payload_size, X2AutosaveStorageFault fault) {
  unsigned char size_le[4];
  const uint32_t payload_u32 = (uint32_t)payload_size;
  size_le[0] = (unsigned char)payload_u32;
  size_le[1] = (unsigned char)(payload_u32 >> 8);
  size_le[2] = (unsigned char)(payload_u32 >> 16);
  size_le[3] = (unsigned char)(payload_u32 >> 24);
  return write_complete(file_fd, header, X2_SAVE_HEADER_BYTES) &&
         !injected(fault, X2_AUTOSAVE_FAULT_AFTER_HEADER) &&
         write_complete(file_fd, size_le, sizeof size_le) &&
         !injected(fault, X2_AUTOSAVE_FAULT_AFTER_LENGTH) &&
         write_complete(file_fd, payload, payload_size) &&
         !injected(fault, X2_AUTOSAVE_FAULT_AFTER_PAYLOAD) &&
         fsync(file_fd) == 0 &&
         !injected(fault, X2_AUTOSAVE_FAULT_AFTER_FILE_SYNC);
}

#if defined(_WIN32)
/* MOVEFILE_WRITE_THROUGH stands in for the POSIX directory fsync. */
static int publish(const char *directory, const void *header,
                   const void *payload, size_t payload_size,
                   X2AutosaveStorageFault fault) {
  static unsigned long sequence;
  char temporary[MAX_PATH];
  char destination[MAX_PATH];
  int file_fd;
  int published = 0;
  int saved_errno = 0;

  if (snprintf(temporary, sizeof temporary, "%s/.autosave.save.tmp.%ld.%lu",
               directory, (long)getpid(),
               ++sequence) >= (int)sizeof temporary ||
      snprintf(destination, sizeof destination, "%s/%s", directory,
               X2_AUTOSAVE_LEAF) >= (int)sizeof destination) {
    errno = ENAMETOOLONG;
    return 0;
  }
  file_fd = open(temporary, O_WRONLY | O_CREAT | O_EXCL | O_BINARY, 0600);
  if (file_fd < 0)
    return 0;
  if (!write_record(file_fd, header, payload, payload_size, fault))
    saved_errno = errno ? errno : EIO;
  if (close(file_fd) != 0 && !saved_errno)
    saved_errno = errno;
  if (!saved_errno) {
    if (injected(fault, X2_AUTOSAVE_FAULT_BEFORE_RENAME))
      saved_errno = errno;
    else if (MoveFileExA(temporary, destination,
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
      published = 1;
    else
      saved_errno = EIO;
  }
  if (!published)
    (void)unlink(temporary);
  if (!published || saved_errno) {
    errno = saved_errno ? saved_errno : EIO;
    return 0;
  }
  return 1;
}
#else
static int publish(const char *directory, const void *header,
                   const void *payload, size_t payload_size,
                   X2AutosaveStorageFault fault) {
  static unsigned long sequence;
  char temporary[96];
  int directory_fd = -1;
  int file_fd = -1;
  int published = 0;
  int failed = 0;
  int saved_errno = 0;

  directory_fd = open(directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if (directory_fd < 0)
    return 0;
  snprintf(temporary, sizeof temporary, ".autosave.save.tmp.%ld.%lu",
           (long)getpid(), ++sequence);
  file_fd = openat(directory_fd, temporary,
                   O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
  if (file_fd < 0) {
    failed = 1;
    goto done;
  }
  if (!write_record(file_fd, header, payload, payload_size, fault)) {
    failed = 1;
    goto done;
  }
  if (close(file_fd) != 0) {
    file_fd = -1;
    failed = 1;
    goto done;
  }
  file_fd = -1;
  if (injected(fault, X2_AUTOSAVE_FAULT_BEFORE_RENAME) ||
      renameat(directory_fd, temporary, directory_fd, X2_AUTOSAVE_LEAF) != 0) {
    failed = 1;
    goto done;
  }
  published = 1;
  if (fsync(directory_fd) != 0)
    failed = 1;

done:
  if (failed)
    saved_errno = errno ? errno : EIO;
  if (file_fd >= 0 && close(file_fd) != 0 && !saved_errno)
    saved_errno = errno;
  if (!published)
    unlinkat(directory_fd, temporary, 0);
  if (directory_fd >= 0 && close(directory_fd) != 0 && !saved_errno)
    saved_errno = errno;
  if (!published || failed || saved_errno) {
    errno = saved_errno ? saved_errno : EIO;
    return 0;
  }
  return 1;
}
#endif

int x2_autosave_storage_publish(const char *directory, const void *header,
                                const void *payload, size_t payload_size,
                                X2AutosaveStorageFault fault) {
  if (!directory || !directory[0] || !header || (!payload && payload_size) ||
      payload_size > UINT32_MAX || fault < X2_AUTOSAVE_FAULT_NONE ||
      fault > X2_AUTOSAVE_FAULT_BEFORE_RENAME) {
    errno = EINVAL;
    return 0;
  }
  return publish(directory, header, payload, payload_size, fault);
}
