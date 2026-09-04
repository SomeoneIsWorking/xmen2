#include "x86_engine_dispatch.h"

#include "x2_log.h"
#include "x86_engine_intercept.h"
#include "x86_engine_private.h"
#include "x86_guest_call_stack.h"
#include "x86_import_fastpath.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include "cpu.h"

#include <stdint.h>
#include <stdlib.h>

/* ---- servicing an interception point ---------------------------------- */

static const X86GuestCallFrame *require_call_frame(struct X86pCpu *cpu) {
  const X86GuestCallFrame *frame = x86_guest_call_for_cpu(cpu);
  if (frame)
    return frame;
  x2_log_error("engine: dispatch at 0x%08x has no matching canonical CPU "
               "context\n",
               cpu->eip);
  abort();
}

void x86_engine_run_host_at(struct X86pCpu *cpu) {
  (void)require_call_frame(cpu);
  /* Eligible native imports run directly on the canonical x86port state. */
  if (__builtin_expect(x86_import_fastpath_dispatch(cpu), 0)) {
    x2_engine_note_callout();
    return;
  }
  const uint32_t target = cpu->eip;
  /*
   * The return address is read HERE, before the body runs. After it, ESP is
   * past that word by however many argument bytes the callee popped -- a
   * __stdcall body pops its own and a __cdecl body pops none -- so reading it
   * back relative to the returned ESP would name the return address for cdecl
   * and a stack argument for everything else.
   */
  const uint32_t ret = RD32(cpu->reg[kX86pEsp]);
  x2_engine_note_callout();
  x86_dispatch(cpu, target);
  /* The dispatched body emulated its own RET, so the guest ESP it returns
     with is already right. Only EIP is this loop's to restore. */
  cpu->eip = ret;
}

X86pJitDispatchResult x86_engine_jit_dispatch(struct X86pCpu *cpu, void *user) {
  (void)user;
  const X86GuestCallFrame *ctx = require_call_frame(cpu);
  const uint32_t eip = cpu->eip;

  /* The cases that need the title call loop's own host frame back. */
  if (eip == ctx->return_to && cpu->reg[kX86pEsp] >= ctx->entry_esp + 4u)
    return kX86pDispatchUnwind;
  if (eip != ctx->entry && x86_setjmp3_thunk(eip))
    return kX86pDispatchUnwind;
  if (eip == ENGINE_RETURN_ADDR)
    return kX86pDispatchUnwind;

  if (!x86_engine_host_body_at(eip, ctx->entry))
    return kX86pDispatchUnwind; /* the intercept predicate saw something this
                                   handler does not own -- hand it back */

  x86_engine_run_host_at(cpu);
  return kX86pDispatchContinue;
}
