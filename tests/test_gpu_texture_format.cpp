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
  if (gpu_texture_level_bytes(GPU_FMT_BGR8, 3, 2) != 24u ||
      gpu_texture_level_bytes(GPU_FMT_BC1, 5, 4) != 16u ||
      gpu_texture_level_bytes(GPU_FMT_BC3, 5, 5) != 64u ||
      gpu_texture_level_bytes(GPU_FMT_RGBA8, 3, 3) != 36u) {
    fprintf(stderr, "gpu texture format: a level's byte count is wrong -- "
                    "BGR8 is stored expanded and BC formats round up to "
                    "whole 4x4 blocks\n");
    return 1;
  }
  puts("gpu texture format: BGR8 expands to opaque BGRA8, and level sizes "
       "count expanded pixels and whole blocks");
  return 0;
}
