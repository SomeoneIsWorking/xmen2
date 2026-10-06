/*
 * Differential gate for the igAttrStackManager::reset native override.
 *
 * Gated on runtime cvar sg.attr_stack_verify. Snapshots attr stack states
 * before native reset, runs the native loop, restores the snapshot, runs
 * the guest body via x86_guest_body, and asserts bit-for-bit equivalence.
 */
#include "attr_stack_verify.h"

#include "guest_body.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <lucent/cvar_c.h>

static int verify_enabled(void) {
  static int cached = -1;
  if (cached < 0)
    cached = lucent_cvar_flag("sg.attr_stack_verify", 0) ? 1 : 0;
  return cached;
}

void attr_stack_verify_begin(AttrStackVerify *v, uint32_t self) {
  memset(v, 0, sizeof *v);
  if (!verify_enabled())
    return;

  v->active = 1;
  v->self = self;
  int count = (int)RD32(self + 0x0cu);
  if (count > 128)
    count = 128;
  v->count = count;

  if (count > 0) {
    uint32_t list = RD32(self + 0x10u);
    uint32_t array = RD32(list + 0x10u);
    for (int i = 0; i < count; i++) {
      uint32_t stack = RD32(array + (uint32_t)i * 4u);
      v->stacks[i] = stack;
      v->orig_f08[i] = RD32(stack + 0x08u);
      v->orig_f18[i] = RD32(stack + 0x18u);
      v->orig_f24[i] = RD32(stack + 0x24u);
      v->orig_f20[i] = RD8(stack + 0x20u);
      v->orig_f28[i] = RD8(stack + 0x28u);
      v->orig_f30[i] = RD32(stack + 0x30u);
    }
  }

  uint32_t p18 = RD32(self + 0x18u);
  v->p18 = p18;
  if (p18)
    v->orig_p18_f8 = RD32(p18 + 0x8u);

  uint32_t p1c = RD32(self + 0x1cu);
  v->p1c = p1c;
  if (p1c)
    v->orig_p1c_f8 = RD32(p1c + 0x8u);

  uint32_t p2c = RD32(self + 0x2cu);
  v->p2c = p2c;
  if (p2c)
    v->orig_p2c_f8 = RD32(p2c + 0x8u);

  uint32_t p40 = RD32(self + 0x40u);
  v->p40 = p40;
  if (p40)
    v->orig_p40_f14 = RD32(p40 + 0x14u);

  uint32_t p24 = RD32(self + 0x24u);
  v->p24 = p24;
  if (p24)
    v->orig_p24_f8 = RD32(p24 + 0x8u);

  uint32_t p28 = RD32(self + 0x28u);
  v->p28 = p28;
  if (p28)
    v->orig_p28_f8 = RD32(p28 + 0x8u);
}

void attr_stack_verify_end(const CPU *C, AttrStackVerify *v, uint32_t self) {
  if (!v->active)
    return;

  /* Snapshot what native execution wrote */
  uint32_t native_f08[128], native_f18[128], native_f24[128], native_f30[128];
  uint8_t native_f20[128], native_f28[128];
  for (int i = 0; i < v->count; i++) {
    uint32_t stack = v->stacks[i];
    native_f08[i] = RD32(stack + 0x08u);
    native_f18[i] = RD32(stack + 0x18u);
    native_f24[i] = RD32(stack + 0x24u);
    native_f20[i] = RD8(stack + 0x20u);
    native_f28[i] = RD8(stack + 0x28u);
    native_f30[i] = RD32(stack + 0x30u);
  }

  uint32_t native_p18_f8 = v->p18 ? RD32(v->p18 + 0x8u) : 0;
  uint32_t native_p1c_f8 = v->p1c ? RD32(v->p1c + 0x8u) : 0;
  uint32_t native_p2c_f8 = v->p2c ? RD32(v->p2c + 0x8u) : 0;
  uint32_t native_p40_f14 = v->p40 ? RD32(v->p40 + 0x14u) : 0;
  uint32_t native_p24_f8 = v->p24 ? RD32(v->p24 + 0x8u) : 0;
  uint32_t native_p28_f8 = v->p28 ? RD32(v->p28 + 0x8u) : 0;

  /* Restore initial state for guest re-run */
  for (int i = 0; i < v->count; i++) {
    uint32_t stack = v->stacks[i];
    WR32(stack + 0x08u, v->orig_f08[i]);
    WR32(stack + 0x18u, v->orig_f18[i]);
    WR32(stack + 0x24u, v->orig_f24[i]);
    WR8(stack + 0x20u, v->orig_f20[i]);
    WR8(stack + 0x28u, v->orig_f28[i]);
    WR32(stack + 0x30u, v->orig_f30[i]);
  }
  if (v->p18)
    WR32(v->p18 + 0x8u, v->orig_p18_f8);
  if (v->p1c)
    WR32(v->p1c + 0x8u, v->orig_p1c_f8);
  if (v->p2c)
    WR32(v->p2c + 0x8u, v->orig_p2c_f8);
  if (v->p40)
    WR32(v->p40 + 0x14u, v->orig_p40_f14);
  if (v->p24)
    WR32(v->p24 + 0x8u, v->orig_p24_f8);
  if (v->p28)
    WR32(v->p28 + 0x8u, v->orig_p28_f8);

  /* Re-run guest body */
  CPU guest = *C;
  guest.reg[kX86pEcx] = self;
  x86_guest_body(&guest, "libIGSg.dll", 0x10034d30u);

  /* Compare each stack */
  for (int i = 0; i < v->count; i++) {
    uint32_t stack = v->stacks[i];
    assert(RD32(stack + 0x08u) == native_f08[i]);
    assert(RD32(stack + 0x18u) == native_f18[i]);
    assert(RD32(stack + 0x24u) == native_f24[i]);
    assert(RD8(stack + 0x20u) == native_f20[i]);
    assert(RD8(stack + 0x28u) == native_f28[i]);
    assert(RD32(stack + 0x30u) == native_f30[i]);
  }
  if (v->p18)
    assert(RD32(v->p18 + 0x8u) == native_p18_f8);
  if (v->p1c)
    assert(RD32(v->p1c + 0x8u) == native_p1c_f8);
  if (v->p2c)
    assert(RD32(v->p2c + 0x8u) == native_p2c_f8);
  if (v->p40)
    assert(RD32(v->p40 + 0x14u) == native_p40_f14);
  if (v->p24)
    assert(RD32(v->p24 + 0x8u) == native_p24_f8);
  if (v->p28)
    assert(RD32(v->p28 + 0x8u) == native_p28_f8);
}
