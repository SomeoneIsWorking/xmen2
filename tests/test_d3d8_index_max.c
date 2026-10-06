/*
 * d3d8_index_max: the largest index of a range, remembered by the upload
 * serial of the buffer holding it. A repeat under the same serial is answered
 * without reading the indices (unless another range took its slot since); a new
 * serial, another range, or serial 0 reads them again.
 */
#include "d3d8_draw_range.h"

#include <stdio.h>

/* d3d8_draw_range.c's other dependencies; this test calls d3d8_index_max
   alone. */
uintptr_t g_guest_memory_base;
uint64_t gpu_buffer_serial(GpuBuffer b) {
  (void)b;
  return 0;
}
uint32_t d3d8_element_count(uint32_t primitive_type, uint32_t primitive_count) {
  (void)primitive_type;
  return primitive_count;
}

static int failures;

static void expect(const char *what, uint32_t got, uint32_t want) {
  if (got != want) {
    fprintf(stderr, "FAIL %s: got %u, want %u\n", what, got, want);
    failures++;
  }
}

int main(void) {
  uint16_t small[6] = {3, 9, 2, 7, 1, 4};
  const uint32_t wide[3] = {70000u, 5u, 123456u};

  expect("16-bit, the whole range", d3d8_index_max(1u, small, 0, 0, 6), 9u);
  small[1] = 0;
  expect("the same upload is answered without reading",
         d3d8_index_max(1u, small, 0, 0, 6), 9u);
  expect("a new upload is read again", d3d8_index_max(3u, small, 0, 0, 6), 7u);
  expect("another range of that upload is read",
         d3d8_index_max(3u, small, 0, 4, 2), 4u);
  /* Uploads that share serial 1's slot must not take its answer: 20000
     serials over 4096 slots put several in it. */
  for (uint32_t serial = 4; serial < 20000u; serial++) {
    if (d3d8_index_max(serial, small, 0, 0, 6) != 7u) {
      expect("an upload sharing a slot is read", 9u, 7u);
      break;
    }
  }
  expect("32-bit indices above 16 bits", d3d8_index_max(2u, wide, 1, 0, 3),
         123456u);

  small[5] = 11;
  expect("serial 0 always reads", d3d8_index_max(0u, small, 0, 0, 6), 11u);
  small[5] = 1;
  expect("serial 0 reads again", d3d8_index_max(0u, small, 0, 0, 6), 7u);

  if (failures) {
    fprintf(stderr, "%d failure(s)\n", failures);
    return 1;
  }
  printf("d3d8_index_max: ok\n");
  return 0;
}
