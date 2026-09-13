#include "guest_file_io.h"

#include "guest_memory.h"

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <unistd.h>

enum { X2_FILE_CHUNK = 16384 };

static int valid_guest_span(uint32_t address, size_t bytes) {
  if (bytes > UINT32_MAX || (uint64_t)address + bytes > (UINT64_C(1) << 32)) {
    errno = EFAULT;
    return 0;
  }
  return 1;
}

static size_t chunk_size(size_t remaining) {
  return remaining < X2_FILE_CHUNK ? remaining : X2_FILE_CHUNK;
}

size_t x2_guest_fread(uint32_t destination, size_t size, size_t count,
                      FILE *stream) {
  unsigned char buffer[X2_FILE_CHUNK];
  size_t transferred = 0;
  size_t requested;
  if (!size || !count) {
    return 0;
  }
  if (count > SIZE_MAX / size) {
    errno = EOVERFLOW;
    return 0;
  }
  requested = size * count;
  if (!valid_guest_span(destination, requested)) {
    return 0;
  }
  while (transferred < requested) {
    size_t ask = chunk_size(requested - transferred);
    size_t received = fread(buffer, 1, ask, stream);
    if (received) {
      guest_memory_write(destination + (uint32_t)transferred, buffer, received);
      transferred += received;
    }
    if (received < ask) {
      break;
    }
  }
  return transferred / size;
}

size_t x2_guest_fwrite(uint32_t source, size_t size, size_t count,
                       FILE *stream) {
  unsigned char buffer[X2_FILE_CHUNK];
  size_t transferred = 0;
  size_t requested;
  if (!size || !count) {
    return 0;
  }
  if (count > SIZE_MAX / size) {
    errno = EOVERFLOW;
    return 0;
  }
  requested = size * count;
  if (!valid_guest_span(source, requested)) {
    return 0;
  }
  while (transferred < requested) {
    size_t ask = chunk_size(requested - transferred);
    guest_memory_read(source + (uint32_t)transferred, buffer, ask);
    size_t written = fwrite(buffer, 1, ask, stream);
    transferred += written;
    if (written < ask) {
      break;
    }
  }
  return transferred / size;
}

ssize_t x2_guest_read_fd(int fd, uint32_t destination, size_t bytes) {
  unsigned char buffer[X2_FILE_CHUNK];
  size_t transferred = 0;
  if (bytes > SSIZE_MAX) {
    errno = EOVERFLOW;
    return -1;
  }
  if (!valid_guest_span(destination, bytes)) {
    return -1;
  }
  while (transferred < bytes) {
    size_t ask = chunk_size(bytes - transferred);
    ssize_t received = read(fd, buffer, ask);
    if (received < 0) {
      return transferred ? (ssize_t)transferred : -1;
    }
    if (!received) {
      break;
    }
    guest_memory_write(destination + (uint32_t)transferred, buffer,
                       (size_t)received);
    transferred += (size_t)received;
    if ((size_t)received < ask) {
      break;
    }
  }
  return (ssize_t)transferred;
}

ssize_t x2_guest_write_fd(int fd, uint32_t source, size_t bytes) {
  unsigned char buffer[X2_FILE_CHUNK];
  size_t transferred = 0;
  if (bytes > SSIZE_MAX) {
    errno = EOVERFLOW;
    return -1;
  }
  if (!valid_guest_span(source, bytes)) {
    return -1;
  }
  while (transferred < bytes) {
    size_t ask = chunk_size(bytes - transferred);
    guest_memory_read(source + (uint32_t)transferred, buffer, ask);
    ssize_t written = write(fd, buffer, ask);
    if (written < 0) {
      return transferred ? (ssize_t)transferred : -1;
    }
    if (!written) {
      break;
    }
    transferred += (size_t)written;
    if ((size_t)written < ask) {
      break;
    }
  }
  return (ssize_t)transferred;
}
