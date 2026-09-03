#include "x86_engine_intercept.h"

#include "x86_engine_frames.h"
#include "x86_engine_internal.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include "cpu.h"

int x86_engine_intercepts_addr(uint32_t eip) {
  return x86_is_thunk(eip) || eip == ENGINE_RETURN_ADDR ||
         x86_native_body_at(eip);
}

int x86_engine_jit_intercept(const struct X86pCpu *cpu, void *user) {
  (void)user;
  const uint32_t eip = cpu->eip;
  if (x86_is_thunk(eip) || eip == ENGINE_RETURN_ADDR)
    return 1;
  {
    const EngineFrame *f = engine_frame_top();
    if (f && eip == f->return_to && cpu->reg[kX86pEsp] >= f->entry_esp + 4u)
      return 1;
    /* The selftest enters a body at its own entry deliberately, to run it both
       ways and compare; every other arrival at a native body is a hand-back.
       Not gated on `f`: it stops recording past ENGINE_FRAMES_MAX, and gating
       here silently disabled JIT interception of every override reached
       through a boot call chain deeper than 64. */
    if (f && eip == f->entry)
      return 0;
    return x86_native_body_at(eip);
  }
}

int x86_engine_jit_boundary(uint32_t eip, void *user) {
  (void)user;
  return x86_engine_intercepts_addr(eip) || x86_setjmp3_thunk(eip);
}
