#include "platform_file_map.h"

#include <assert.h>

int main(void) {
  X2FileMap mapping = {0};

  assert(x2_file_map_readonly(__FILE__, &mapping) == 0);
  assert(mapping.address != NULL);
  assert(mapping.size > 0);
  assert(((const unsigned char *)mapping.address)[0] == '#');
  x2_file_unmap(&mapping);
  assert(mapping.address == NULL);
  assert(mapping.size == 0);

  assert(x2_file_map_readonly("this-file-does-not-exist", &mapping) != 0);
  assert(mapping.address == NULL);
  assert(mapping.size == 0);
  return 0;
}
