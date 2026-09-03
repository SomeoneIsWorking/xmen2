/*
 * Fast-path native overrides for igAttrStack::customReset and
 * igAttrStackManager::reset (libIGSg.dll 0x10034d10 and 0x10034d30).
 *
 * In a 2000-frame in-game workload, igAttrStackManager::reset loops over
 * all active attribute stacks, calling customReset on each, totaling ~3.26M
 * iterations (9.8M JIT block entries, 2.4% of total execution time).
 *
 * Running this loop natively in host C avoids ~3.26M function call/return
 * frames and JIT block boundaries per 2000 frames.
 */
#include "attr_stack.h"
#include "attr_stack_verify.h"

#include "guest_body.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include <lucent/cvar_c.h>

#if defined(TEST_SUITE)
static int attr_stack_enabled(void) { return 1; }
#else
static int attr_stack_enabled(void) {
  static int cached = -1;
  if (__builtin_expect(cached < 0, 0))
    cached = lucent_cvar_flag("sg.attr_stack", 1) ? 1 : 0;
  return cached;
}
#endif

static uint32_t s_clear_light_handles_mapped;

static void call_clear_light_handles(CPU *C, uint32_t self) {
  if (__builtin_expect(!s_clear_light_handles_mapped, 0)) {
    char why[128];
    if (x86_override_resolve_check("libIGSg.dll", 0x10035950u,
                                   &s_clear_light_handles_mapped,
                                   why, sizeof why) != 0) {
      return;
    }
  }
  CPU call_cpu = *C;
  call_cpu.ecx = self;
  x86_guest_call_args(&call_cpu, s_clear_light_handles_mapped, 0u);
}

void attr_stack_custom_reset(uint32_t stack) {
  const uint32_t f14 = RD32(stack + 0x14u);
  WR32(stack + 0x08u, 0);
  WR32(stack + 0x18u, 0xffffffffu);
  WR32(stack + 0x24u, f14);
  WR8(stack + 0x20u, 0);
  WR8(stack + 0x28u, 0);
  WR32(stack + 0x30u, 0);
}

void x2_override_10034d10(CPU *C) {
  if (__builtin_expect(!attr_stack_enabled(), 0)) {
    x86_guest_body(C, "libIGSg.dll", 0x10034d10u);
    return;
  }
  attr_stack_custom_reset(C->ecx);
  C->esp += 4u;
}

void x2_override_10034d30(CPU *C) {
  if (__builtin_expect(!attr_stack_enabled(), 0)) {
    x86_guest_body(C, "libIGSg.dll", 0x10034d30u);
    return;
  }
  const uint32_t self = C->ecx;
  AttrStackVerify v;
  attr_stack_verify_begin(&v, self);

  const int count = (int)RD32(self + 0x0cu);
  if (count > 0) {
    const uint32_t list = RD32(self + 0x10u);
    const uint32_t array = RD32(list + 0x10u);
    for (int i = 0; i < count; i++) {
      const uint32_t stack = RD32(array + (uint32_t)i * 4u);
      attr_stack_custom_reset(stack);
    }
  }

  const uint32_t p18 = RD32(self + 0x18u);
  if (p18)
    WR32(p18 + 0x8u, 0);
  const uint32_t p1c = RD32(self + 0x1cu);
  if (p1c)
    WR32(p1c + 0x8u, 0);

  /* igAttrStackManager::clearLightHandles(0x10035950) with ecx = self */
  call_clear_light_handles(C, self);

  const uint32_t p2c = RD32(self + 0x2cu);
  if (p2c)
    WR32(p2c + 0x8u, 0);
  const uint32_t p40 = RD32(self + 0x40u);
  if (p40)
    WR32(p40 + 0x14u, 0);

  attr_stack_verify_end(C, &v, self);
  C->esp += 4u;
}

__attribute__((constructor)) static void register_attr_stack_overrides(void) {
  x86_register_override("libIGSg.dll", 0x10034d10u, x2_override_10034d10);
  x86_register_override("libIGSg.dll", 0x10034d30u, x2_override_10034d30);
}
