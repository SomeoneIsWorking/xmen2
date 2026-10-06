/*
 * Native stand-in for libIGGfx.dll!0x10046ce0 -- the vertex-array range colour
 * swap. See vertex_color_swizzle.h for the algorithm and why (issue #141: the
 * per-vertex swap loop was ~4.4% of in-game guest wall time, the biggest single
 * block after the ADPCM and _ftol2 clusters were natively owned).
 *
 * The function is a shared virtual method of igDxVertexArray1_1 and
 * igDx8VertexArray1_1 (same vtable slot in both), reached only through the
 * vtable; one override at the linked address covers every caller.
 */
#include "vertex_color_swizzle.h"

#include "x86rt_native.h"

#include <stdint.h>

/* Descriptor (arg1) and `this` field offsets, from the retail disassembly. */
enum {
  DESC_TYPE = 0x04,  /* == 2 selects the colour path */
  DESC_START = 0x08, /* first vertex index */
  DESC_COUNT = 0x0c, /* vertex count */

  SELF_BUFINFO = 0x08, /* -> buffer-info struct; +0x50 is the vertex base */
  BUFINFO_BASE = 0x50,
  SELF_STRIDE = 0x38, /* vertex stride, one byte */
  SELF_COLOFF = 0x3b, /* colour field offset within a vertex, one byte */
  SELF_DIRTY = 0x60,  /* dirty-flag byte */
  SELF_LOCK = 0x68,   /* lock counter byte */

  DESC_TYPE_COLOR = 2,
};

uint32_t vtx_color_swizzle_word(uint32_t w) {
  const uint32_t x = (w ^ (w >> 16)) & 0x000000ffu;
  return w ^ x ^ (x << 16);
}

static void swizzle_range(uint32_t self, uint32_t desc) {
  if (RD32(desc + DESC_TYPE) != DESC_TYPE_COLOR)
    return;

  const uint32_t count = RD32(desc + DESC_COUNT);
  if (count == 0)
    return;

  const uint32_t base = RD32(RD32(self + SELF_BUFINFO) + BUFINFO_BASE);
  const uint32_t stride = RD8(self + SELF_STRIDE);
  const uint32_t coloff = RD8(self + SELF_COLOFF);
  const uint32_t start = RD32(desc + DESC_START);

  if (__builtin_expect(x2_write_watch_addr != 0, 0)) {
    const uint32_t end = start + count;
    for (uint32_t i = start; i < end; i++) {
      const uint32_t p = base + stride * i + coloff;
      WR32(p, vtx_color_swizzle_word(RD32(p)));
    }
    return;
  }

  uint8_t *p = (uint8_t *)x86_guest_pointer(base + stride * start + coloff);
  for (uint32_t i = 0; i < count; i++, p += stride) {
    const uint8_t t = p[0];
    p[0] = p[2];
    p[2] = t;
  }
}

/* The dirty/lock bookkeeping the guest runs after the swap, whether or not the
   colour path ran. `flags` is the call's second argument. */
static void update_flags(uint32_t self, uint32_t flags) {
  const uint8_t lock = (uint8_t)RD8(self + SELF_LOCK);
  if (flags & 1u) {
    WR8(self + SELF_LOCK, (uint8_t)(lock - 1u));
    return;
  }
  uint8_t dirty = (uint8_t)RD8(self + SELF_DIRTY);
  dirty |= (flags & 2u) ? 2u : 1u;
  WR8(self + SELF_DIRTY, dirty);
  WR8(self + SELF_LOCK, (uint8_t)(lock - 1u));
}

void x2_override_10046ce0(CPU *C) {
  const uint32_t self = C->reg[kX86pEcx];
  const uint32_t desc = RD32(C->reg[kX86pEsp] + 4u);
  const uint32_t flags = RD32(C->reg[kX86pEsp] + 8u);

  VtxSwizzleVerify v;
  vtx_swizzle_verify_begin(&v, self, desc);

  swizzle_range(self, desc);
  update_flags(self, flags);

  vtx_swizzle_verify_end(C, &v, self, desc, flags);

  C->reg[kX86pEsp] +=
      12u; /* ret 8: pop the return address and the two dword args */
}

__attribute__((constructor)) static void vertex_color_swizzle_register(void) {
  x86_register_override("libIGGfx.dll", 0x10046ce0u, x2_override_10046ce0);
}
