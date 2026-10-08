#include "platform_file_map.h"

#include <assert.h>

int main(void) {
  x2::native::FileMap mapping = {0};

  assert(x2::native::file_map_readonly(__FILE__, &mapping) == 0);
  assert(mapping.address != NULL);
  assert(mapping.size > 0);
  assert(((const unsigned char *)mapping.address)[0] == '#');
  x2::native::file_unmap(&mapping);
  assert(mapping.address == NULL);
  assert(mapping.size == 0);

  assert(x2::native::file_map_readonly("this-file-does-not-exist", &mapping) !=
         0);
  assert(mapping.address == NULL);
  assert(mapping.size == 0);
  return 0;
}
