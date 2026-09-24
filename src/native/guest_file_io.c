#include "guest_file_io.h"

#include "guest_memory.h"

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <unistd.h>

/*
 * One host call per guest transfer, straight through the guest pointer.
 *
 * The guest space is one linear window, so a guest range is a host range and
 * the file layer reads into it directly. It used to bounce through a 16 KB
 * stack buffer from when pages were separate allocations; that made every
 * 16 KB its own host read, and in the browser each host read is a synchronous
 * proxy to the OPFS thread -- measured at 2.45% of the guest worker during
 * play. A bad span is refused by guest_memory_span before the call, not after
 * a partial transfer.
 */
static int span_bytes(size_t size, size_t count, size_t *bytes) {
  if (count > SIZE_MAX / size) {
    errno = EOVERFLOW;
    return 0;
  }
  *bytes = size * count;
  return 1;
}

size_t x2_guest_fread(uint32_t destination, size_t size, size_t count,
                      FILE *stream) {
  size_t bytes;
  void *host;
  if (!size || !count || !span_bytes(size, count, &bytes)) {
    return 0;
  }
  host = guest_memory_span(destination, bytes);
  return host ? fread(host, size, count, stream) : 0;
}

size_t x2_guest_fwrite(uint32_t source, size_t size, size_t count,
                       FILE *stream) {
  size_t bytes;
  const void *host;
  if (!size || !count || !span_bytes(size, count, &bytes)) {
    return 0;
  }
  host = guest_memory_span(source, bytes);
  return host ? fwrite(host, size, count, stream) : 0;
}

ssize_t x2_guest_read_fd(int fd, uint32_t destination, size_t bytes) {
  void *host;
  if (bytes > SSIZE_MAX) {
    errno = EOVERFLOW;
    return -1;
  }
  if (!bytes) {
    return 0;
  }
  host = guest_memory_span(destination, bytes);
  return host ? read(fd, host, bytes) : -1;
}

ssize_t x2_guest_write_fd(int fd, uint32_t source, size_t bytes) {
  const void *host;
  if (bytes > SSIZE_MAX) {
    errno = EOVERFLOW;
    return -1;
  }
  if (!bytes) {
    return 0;
  }
  host = guest_memory_span(source, bytes);
  return host ? write(fd, host, bytes) : -1;
}
