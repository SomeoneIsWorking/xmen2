#include "x86_engine_intercept.h"

#include "x86_engine_private.h"
#include "x86_guest_call_stack.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include "cpu.h"

#include <stdint.h>

int x86_engine_host_body_at(uint32_t eip, uint32_t entry) {
  if (__builtin_expect((uint32_t)(eip - 0x00080000u) < 0x50000u, 0))
    return x86_is_thunk(eip);
  if (eip != entry && x86_override_bloom_has(eip))
    return x86_native_body_at(eip);
  return 0;
}

int x86_engine_intercepts_addr(uint32_t eip) {
  if (__builtin_expect((uint32_t)(eip - 0x00080000u) < 0x50000u, 0)) {
    if (x86_is_thunk(eip) || eip == ENGINE_RETURN_ADDR)
      return 1;
  }
  if (__builtin_expect(!x86_override_bloom_has(eip), 1))
    return 0;
  return x86_native_body_at(eip);
}

int x86_engine_jit_intercept(const struct X86pCpu *cpu, void *user, void *run_user) {
  (void)user;
  const uint32_t eip = cpu->eip;
  if (__builtin_expect((uint32_t)(eip - 0x00080000u) < 0x50000u, 0)) {
    if (x86_is_thunk(eip) || eip == ENGINE_RETURN_ADDR)
      return 1;
  }
  {
    /* The run's own frame, handed down by x86port. Reading the thread-local
       chain head here instead cost a call through a pthread key on every block
       boundary, because an Android shared object below API 29 has emulated
       thread-locals: 13.4% of the port library's samples. */
    const X86GuestCallFrame *f = (const X86GuestCallFrame *)run_user;
    if (__builtin_expect(f != NULL, 1)) {
      if (eip == f->return_to && cpu->reg[kX86pEsp] >= f->entry_esp + 4u)
        return 1;
    }
    if (__builtin_expect(!x86_override_bloom_has(eip), 1))
      return 0;
    /* The selftest enters a body at its own entry deliberately, to run it both
       ways and compare; every other arrival at a native body is a hand-back. */
    if (f && eip == f->entry)
      return 0;
    return x86_native_body_at(eip);
  }
}

int x86_engine_jit_boundary(uint32_t eip, void *user) {
  (void)user;
  return x86_engine_intercepts_addr(eip) || x86_setjmp3_thunk(eip);
}
