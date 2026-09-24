#ifndef X2_GUEST_FILE_IO_H
#define X2_GUEST_FILE_IO_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>

/* Transfer file bytes directly into or out of the guest window. A range that
 * is null or runs past 4 GB is refused with EFAULT before any transfer. */
size_t x2_guest_fread(uint32_t destination, size_t size, size_t count,
                      FILE *stream);
size_t x2_guest_fwrite(uint32_t source, size_t size, size_t count,
                       FILE *stream);
ssize_t x2_guest_read_fd(int fd, uint32_t destination, size_t bytes);
ssize_t x2_guest_write_fd(int fd, uint32_t source, size_t bytes);

#endif
