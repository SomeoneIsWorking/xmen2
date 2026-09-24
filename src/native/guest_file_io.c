#include "guest_file_io.h"

#include "guest_memory.h"

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <unistd.h>

/*
 * One host call per guest transfer, straight through the guest pointer.
 *
 * The guest space is one linear window (guest_memory_pointer is base +
 * address on every target), so a guest range is a host range and the file
 * layer can read into it directly. It used to bounce through a 16 KB stack
 * buffer from when pages were separate allocations; that made every 16 KB its
 * own host read, and in the browser each host read is a synchronous proxy to
 * the OPFS thread -- measured at 2.45% of the guest worker during play.
 *
 * The span is refused before the call, not after a partial transfer, so a bad
 * guest range is reported as one rather than as a short read.
 */
static void *guest_span(uint32_t address, size_t bytes) {
  if (!address || bytes > UINT32_MAX ||
      (uint64_t)address + bytes > (UINT64_C(1) << 32)) {
    errno = EFAULT;
    return NULL;
  }
  /* The last byte through the same owner: in the browser that is the window
     check, which reports an address past the window by name. */
  (void)guest_memory_pointer(address + (uint32_t)(bytes - 1u));
  return guest_memory_pointer(address);
}

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
  host = guest_span(destination, bytes);
  return host ? fread(host, size, count, stream) : 0;
}

size_t x2_guest_fwrite(uint32_t source, size_t size, size_t count,
                       FILE *stream) {
  size_t bytes;
  const void *host;
  if (!size || !count || !span_bytes(size, count, &bytes)) {
    return 0;
  }
  host = guest_span(source, bytes);
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
  host = guest_span(destination, bytes);
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
  host = guest_span(source, bytes);
  return host ? write(fd, host, bytes) : -1;
}
