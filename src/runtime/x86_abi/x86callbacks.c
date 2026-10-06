/* Native CRT callback-table adapter for the title ABI. */
#include "x86callbacks.h"
#include "x2_log.h"

#include <stdlib.h>

void x86_host_initterm(CPU *C) {
  uint32_t first = RD32(C->reg[kX86pEsp] + 4u);
  uint32_t last = RD32(C->reg[kX86pEsp] + 8u);
  uint32_t slot;

  if (last < first || ((last - first) & 3u) != 0) {
    x2_log_error("_initterm: invalid callback range 0x%08x..0x%08x\n", first,
                 last);
    abort();
  }
  for (slot = first; slot < last; slot += 4u) {
    uint32_t target = RD32(slot);
    uint32_t before = C->reg[kX86pEsp];
    if (!target)
      continue;
    C->reg[kX86pEsp] -= 4u;
    WR32(C->reg[kX86pEsp], RD32(before));
    x86_dispatch(C, target);
    if (C->reg[kX86pEsp] != before) {
      x2_log_error("_initterm: callback 0x%08x changed esp "
                   "0x%08x -> 0x%08x\n",
                   target, before, C->reg[kX86pEsp]);
      abort();
    }
  }
}
