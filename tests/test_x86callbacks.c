#define _GNU_SOURCE
#include "guest_memory.h"
#include "x86callbacks.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

static uint32_t seen[4];
static int seen_count;

void x86_dispatch(CPU *C, uint32_t target) {
  seen[seen_count++] = target;
  C->esp += 4u;
}

int main(void) {
  CPU C;
  const uint32_t memory = 0x70000000u;
  uint32_t *mem;
  if (guest_memory_init() != 0 ||
      guest_memory_map_fixed(memory, 4096, PROT_READ | PROT_WRITE) != 0) {
    fprintf(stderr, "could not allocate guest callback corpus\n");
    return 1;
  }
  mem = guest_memory_pointer(memory);
  memset(&C, 0, sizeof C);
  /* Leave real downward-growing stack space below the synthetic ESP.  The
     shipping adapter pushes one return word before each callback. */
  mem[64] = 0xabcdef01u;
  mem[4] = 0;
  mem[5] = 0x11111111u;
  mem[6] = 0x22222222u;
  C.esp = memory + 64u * 4u;
  mem[65] = memory + 4u * 4u;
  mem[66] = memory + 7u * 4u;

  x86_host_initterm(&C);
  if (seen_count != 2 || seen[0] != mem[5] || seen[1] != mem[6]) {
    fprintf(stderr,
            "_initterm scanned 3 slots but dispatched %d: "
            "%08x %08x\n",
            seen_count, seen[0], seen[1]);
    return 1;
  }
  if (C.esp != memory + 64u * 4u) {
    fprintf(stderr, "_initterm changed caller esp\n");
    return 1;
  }
  return 0;
}
