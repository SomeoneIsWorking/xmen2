/* d3d8_selftest_call.c -- see d3d8_selftest_call.h. */
#include "d3d8_selftest_call.h"

#include "guest_heap.h"
#include "guest_memory.h"
#include "x86rt.h"

uint32_t d3d8_selftest_call(D3D8Object *object, int slot, const uint32_t *args,
                            int nargs) {
  CPU C;
  static uint32_t stack;
  uint32_t vt = d3d8_iface_vtable(d3d8_object_iface(object));
  int i;

  if (!stack)
    stack = guest_malloc(4096) + 2048;
  cpu_reset(&C);
  C.reg[kX86pEsp] = stack - (uint32_t)(nargs + 2) * 4u;
  WR32(C.reg[kX86pEsp], 0xD3D80000u);                    /* return address */
  WR32(C.reg[kX86pEsp] + 4u, d3d8_object_guest(object)); /* this */
  for (i = 0; i < nargs; i++)
    WR32(C.reg[kX86pEsp] + 8u + (uint32_t)i * 4u, args[i]);
  x86_dispatch(&C, RD32(vt + (uint32_t)slot * 4u));
  return C.reg[kX86pEax];
}
