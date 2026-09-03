#include "gpu_texture_format.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

int main(void) {
  static const uint8_t source[] = {0x10, 0x20, 0x30, 0xaa, 0xbb, 0xcc};
  static const uint8_t expected[] = {0x10, 0x20, 0x30, 0xff,
                                     0xaa, 0xbb, 0xcc, 0xff};
  uint8_t actual[sizeof expected] = {0};

  gpu_bgr8_to_bgra8(source, actual, 2);
  if (memcmp(actual, expected, sizeof expected) != 0) {
    fprintf(stderr, "gpu texture format: BGR8 expansion changed channel "
                    "order or did not supply opaque alpha\n");
    return 1;
  }
  puts("gpu texture format: BGR8 expands to opaque BGRA8");
  return 0;
}
