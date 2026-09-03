/*
 * `gfx.vtx_swizzle_verify` -- the differential gate for the native vertex
 * colour-swap override in vertex_color_swizzle.c.
 *
 * When the flag is set, every native swap is checked against the guest's own
 * body: `begin` snapshots the vertex span the swap will touch plus the two
 * dirty/lock bytes; `end` saves the native results, restores the snapshot,
 * runs libIGGfx.dll!0x10046ce0, and aborts if anything the guest produced
 * differs from what the override produced. Needs the execution engine, which
 * is why it is split from vertex_color_swizzle.c (that file and its unit test
 * stay engine-free).
 */
#include "vertex_color_swizzle.h"

#include "guest_body.h"
#include "x86rt.h"

#include <lucent/cvar_c.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

enum {
  DESC_TYPE = 0x04,
  DESC_START = 0x08,
  DESC_COUNT = 0x0c,
  SELF_BUFINFO = 0x08,
  BUFINFO_BASE = 0x50,
  SELF_STRIDE = 0x38,
  SELF_DIRTY = 0x60,
  SELF_LOCK = 0x68,
  DESC_TYPE_COLOR = 2,
};

static int verify_enabled(void) {
  static int cached = -1;
  if (cached < 0)
    cached = lucent_cvar_flag("gfx.vtx_swizzle_verify", 0) ? 1 : 0;
  return cached;
}

/* The byte span [addr, addr+len) the swap loop writes, or len == 0 when the
   colour path will not run. */
static void touched_span(uint32_t self, uint32_t desc, uint32_t *addr,
                         uint32_t *len) {
  *addr = 0;
  *len = 0;
  if (RD32(desc + DESC_TYPE) != DESC_TYPE_COLOR)
    return;
  const uint32_t base = RD32(RD32(self + SELF_BUFINFO) + BUFINFO_BASE);
  const uint32_t stride = RD8(self + SELF_STRIDE);
  const uint32_t start = RD32(desc + DESC_START);
  const uint32_t count = RD32(desc + DESC_COUNT);
  if (count == 0)
    return;
  *addr = base + stride * start;
  *len = stride * count + 4u; /* + one colour word past the last vertex start */
}

void vtx_swizzle_verify_begin(VtxSwizzleVerify *v, uint32_t self,
                              uint32_t desc) {
  v->before = NULL;
  v->addr = 0;
  v->len = 0;
  v->s60 = 0;
  v->s68 = 0;
  v->active = 0;
  if (!verify_enabled())
    return;

  touched_span(self, desc, &v->addr, &v->len);
  v->s60 = (uint8_t)RD8(self + SELF_DIRTY);
  v->s68 = (uint8_t)RD8(self + SELF_LOCK);
  v->active = 1;
  if (v->len == 0)
    return;
  v->before = malloc(v->len);
  if (!v->before) {
    v->active = 0;
    return;
  }
  for (uint32_t i = 0; i < v->len; i++)
    v->before[i] = (uint8_t)RD8(v->addr + i);
}

void vtx_swizzle_verify_end(const CPU *C, VtxSwizzleVerify *v, uint32_t self,
                            uint32_t desc, uint32_t flags) {
  if (!v->active) {
    free(v->before);
    return;
  }

  /* Save what the native path produced, then put the inputs back. */
  uint8_t *native = NULL;
  if (v->len) {
    native = malloc(v->len);
    if (!native) {
      free(v->before);
      return;
    }
    for (uint32_t i = 0; i < v->len; i++) {
      native[i] = (uint8_t)RD8(v->addr + i);
      WR8(v->addr + i, v->before[i]);
    }
  }
  const uint8_t native_s60 = (uint8_t)RD8(self + SELF_DIRTY);
  const uint8_t native_s68 = (uint8_t)RD8(self + SELF_LOCK);
  WR8(self + SELF_DIRTY, v->s60);
  WR8(self + SELF_LOCK, v->s68);

  /* Run the guest's own body from the same start state. C->esp still points at
     the return address with the two args above it. */
  CPU guest = *C;
  x86_guest_body(&guest, "libIGGfx.dll", 0x10046ce0u);

  int bad = (uint8_t)RD8(self + SELF_DIRTY) != native_s60 ||
            (uint8_t)RD8(self + SELF_LOCK) != native_s68;
  for (uint32_t i = 0; i < v->len && !bad; i++)
    if ((uint8_t)RD8(v->addr + i) != native[i])
      bad = 1;

  if (bad) {
    fprintf(stderr,
            "gfx.vtx_swizzle_verify: native swap of libIGGfx.dll!0x10046ce0 "
            "disagrees with the guest body (self 0x%08x, desc 0x%08x, "
            "flags 0x%x, %u byte(s)). The native swizzle is wrong; not "
            "continuing.\n",
            self, desc, flags, v->len);
    abort();
  }

  free(native);
  free(v->before);
}
