#include "pe_export_search.h"

#include <string.h>

static uint32_t read32(const unsigned char *p, uint32_t at) {
  return (uint32_t)p[at] | (uint32_t)p[at + 1u] << 8 |
         (uint32_t)p[at + 2u] << 16 | (uint32_t)p[at + 3u] << 24;
}

long pe_export_name_index(const unsigned char *image, uint32_t names,
                          uint32_t count, const char *name) {
  uint32_t low = 0, high = count;
  while (low < high) {
    const uint32_t mid = low + (high - low) / 2u;
    const char *entry = (const char *)image + read32(image, names + mid * 4u);
    const int order = strcmp(entry, name);
    if (order == 0) {
      return (long)mid;
    }
    if (order < 0) {
      low = mid + 1u;
    } else {
      high = mid;
    }
  }
  return -1;
}
