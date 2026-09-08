#ifndef X2_PLATFORM_FILE_MAP_H
#define X2_PLATFORM_FILE_MAP_H

#include <stddef.h>

typedef struct X2FileMap {
  void *address;
  size_t size;
} X2FileMap;

/* Map an existing file read-only for the lifetime of `out`.  The caller owns
 * the unmap and must not write through the returned address. */
int x2_file_map_readonly(const char *path, X2FileMap *out);
void x2_file_unmap(X2FileMap *mapping);

#endif
