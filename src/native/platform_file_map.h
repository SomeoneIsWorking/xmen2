#pragma once

#include <cstddef>

namespace x2::native {

struct FileMap {
  void *address;
  std::size_t size;
};

/* Map an existing file read-only for the lifetime of `out`.  The caller owns
 * the unmap and must not write through the returned address. */
int file_map_readonly(const char *path, FileMap *out);
void file_unmap(FileMap *mapping);

} // namespace x2::native
