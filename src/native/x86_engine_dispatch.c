#include "x86_engine_dispatch.h"

#include "x86_engine_intercept.h"
#include "x86_engine_internal.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include "cpu.h"
#include "x87.h"

#include <stdint.h>

/* ---- servicing an interception point ---------------------------------- */

static int is_ftol_thunk(uint32_t addr) {
  static uint32_t s_ftol1 = 0, s_ftol2 = 0;
  static int s_cached = 0;
  if (!s_cached) {
    s_ftol1 = x86_native_thunk("MSVCR71.DLL", "_ftol");
    s_ftol2 = x86_native_thunk("MSVCRT.DLL", "_ftol");
    s_cached = 1;
  }
  return (s_ftol1 && addr == s_ftol1) || (s_ftol2 && addr == s_ftol2);
}

void x86_engine_run_host_at(struct X86pCpu *cpu, struct CPU *host) {
  /* _ftol is the single hottest thunk: an x87 pop to edx:eax, __cdecl, pops
     nothing. Handled without the callout bridge -- it never touches the GP
     register file or guest memory. */
  if (__builtin_expect(is_ftol_thunk(cpu->eip), 0)) {
    long double val = 0.0L;
    x86p_x87_pop(&cpu->x87, &val);
    int64_t result = (int64_t)val;
    cpu->reg[kX86pEax] = (uint32_t)(uint64_t)result;
    cpu->reg[kX86pEdx] = (uint32_t)((uint64_t)result >> 32);
    const uint32_t ret = RD32(cpu->reg[kX86pEsp]);
    cpu->reg[kX86pEsp] += 4u;
    cpu->eip = ret;
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
  x2_engine_callout_from_x86p(cpu, host);
  x2_engine_note_callout();
  x86_dispatch(host, target);
  x2_engine_callout_to_x86p(host, cpu);
  /* The dispatched body emulated its own RET, so the guest ESP it returns
     with is already right. Only EIP is this loop's to restore. */
  cpu->eip = ret;
}

/* ---- the inline dispatch handler ------------------------------------- */

#if defined(__GNUC__) || defined(__clang__)
#define X2_TLS_INTERNAL                                                        \
  __attribute__((visibility("hidden"), tls_model("initial-exec")))
#else
#define X2_TLS_INTERNAL
#endif

static X2_TLS_INTERNAL __thread EngineCallCtx *t_call_ctx;

void x86_engine_call_ctx_push(EngineCallCtx *slot, struct CPU *host,
                              uint32_t entry, uint32_t return_to,
                              uint32_t entry_esp) {
  slot->prev = t_call_ctx;
  slot->host = host;
  slot->entry = entry;
  slot->return_to = return_to;
  slot->entry_esp = entry_esp;
  t_call_ctx = slot;
}

void x86_engine_call_ctx_pop(void) {
  if (t_call_ctx)
    t_call_ctx = t_call_ctx->prev;
}

void x86_engine_call_ctx_restore(EngineCallCtx *slot) { t_call_ctx = slot; }

X86pJitDispatchResult x86_engine_jit_dispatch(struct X86pCpu *cpu, void *user) {
  (void)user;
  const EngineCallCtx *ctx = t_call_ctx;
  const uint32_t eip = cpu->eip;

  /* The cases that need the interpreter loop's own host frame back. */
  if (ctx) {
    if (eip == ctx->return_to && cpu->reg[kX86pEsp] >= ctx->entry_esp + 4u)
      return kX86pDispatchUnwind;
    if (eip != ctx->entry && x86_setjmp3_thunk(eip))
      return kX86pDispatchUnwind;
  } else if (x86_setjmp3_thunk(eip)) {
    return kX86pDispatchUnwind;
  }
  if (eip == ENGINE_RETURN_ADDR)
    return kX86pDispatchUnwind;

  if (!x86_engine_host_body_at(eip, ctx ? ctx->entry : 0u))
    return kX86pDispatchUnwind; /* the intercept predicate saw something this
                                   handler does not own -- hand it back */

  x86_engine_run_host_at(cpu, ctx ? ctx->host : 0);
  return kX86pDispatchContinue;
}
