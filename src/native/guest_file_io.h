#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <sys/types.h>

namespace x2::native {

/* Transfer file bytes directly into or out of the guest window. A range that
 * is null or runs past 4 GB is refused with EFAULT before any transfer. */
std::size_t guest_fread(std::uint32_t destination, std::size_t size,
                        std::size_t count, FILE *stream);
std::size_t guest_fwrite(std::uint32_t source, std::size_t size,
                         std::size_t count, FILE *stream);
ssize_t guest_read_fd(int fd, std::uint32_t destination, std::size_t bytes);
ssize_t guest_write_fd(int fd, std::uint32_t source, std::size_t bytes);

} // namespace x2::native
