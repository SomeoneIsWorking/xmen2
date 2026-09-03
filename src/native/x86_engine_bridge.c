/*
 * x86_engine_bridge.c -- the substrate's CPU and x86port's, in both
 * directions.
 *
 * Its own file because it is the piece with the exactness argument attached:
 * see the boundary note in x86_engine.h for why this is correct only at a
 * Win32 function call, and what each model cannot say about the other. The run
 * loop that uses it is a different concern.
 */
#include "x86_engine_internal.h"

#include "x86rt.h"

#include "cpu.h"
#include "x87.h"

#include <string.h>

/* ---- state bridge ------------------------------------------------------ */

/*
 * The six arithmetic flags travel as a materialised EFLAGS word rather than as
 * a lazy tuple.
 *
 * Both models are lazy and neither can express the other's kinds: the
 * substrate has no AF at all, and x86port has three shift kinds where the
 * substrate has one. Translating kind-to-kind would be a second authority on
 * what a flag means, and a disagreement would be invisible -- a wrong CF
 * surfaces as a branch taken differently much later. Materialising is exact
 * for everything both models hold, and it is exact for AF and DF too at the
 * one place this is used: a Win32 call boundary, where the ABI requires DF
 * clear and nothing a compiler emits reads AF across a call.
 */
void x2_engine_to_x86p(const CPU *C, X86pCpu *out) {
  int i;
  memset(out, 0, sizeof *out);
  out->reg[kX86pEax] = C->eax;
  out->reg[kX86pEcx] = C->ecx;
  out->reg[kX86pEdx] = C->edx;
  out->reg[kX86pEbx] = C->ebx;
  out->reg[kX86pEsp] = C->esp;
  out->reg[kX86pEbp] = C->ebp;
  out->reg[kX86pEsi] = C->esi;
  out->reg[kX86pEdi] = C->edi;
  x86p_flags_set_explicit(&out->flags, x86_eflags(C));
  out->df = 0;
  /*
   * THE SEGMENT BASES, which are per-THREAD and not part of the CPU struct
   * on either side. The substrate reads them from `g_fsbase`/`g_gsbase`
   * thread-locals; x86port keeps them in the CPU because which address the
   * TEB lives at is a property of the process the port builds.
   *
   * Not bridging these is what the first taken module found. `mov eax,
   * fs:[0]` is the opening of every /GS-compiled function's SEH prologue --
   * seven bytes into FUN_004874b0, the first function taken -- and with a
   * zero base it reads guest address 0 instead of the TIB. It faulted at
   * 0x3, which is a plausible-looking null dereference and says nothing at
   * all about segments.
   *
   * One way only: nothing a guest executes changes a segment base. The OS
   * sets it, and on this port that is threads.c.
   */
  out->fs_base = g_fsbase;
  out->gs_base = g_gsbase;
  x86p_x87_reset(&out->x87);
  out->x87.control = (uint16_t)C->fcw;
  /* C0-C3 only. TOP is merged in on read from the engine's own stack, and
     the exception flags are not modelled on the substrate side. */
  out->x87.status = (uint16_t)(C->fsw & 0x4700u);
  /* Deepest first, so ST(0) ends up on top of the engine's stack in the same
     order the substrate holds it. Anything below `depth` is not live. */
  for (i = C->depth - 1; i >= 0; i--)
    x86p_x87_push(&out->x87, C->st[(C->top + i) & 7]);
  for (i = 0; i < 8; i++)
    memcpy(out->xmm[i], C->xmm[i], 16);
}

void x2_engine_from_x86p(const X86pCpu *in, CPU *C) {
  int depth, i;
  C->eax = in->reg[kX86pEax];
  C->ecx = in->reg[kX86pEcx];
  C->edx = in->reg[kX86pEdx];
  C->ebx = in->reg[kX86pEbx];
  C->esp = in->reg[kX86pEsp];
  C->ebp = in->reg[kX86pEbp];
  C->esi = in->reg[kX86pEsi];
  C->edi = in->reg[kX86pEdi];
  SETFLAGS(C, FK_EXPLICIT, x86p_eflags(&in->flags), 0u, 0u, 4);
  C->fcw = in->x87.control;
  C->fsw = (uint32_t)(x86p_x87_status(&in->x87) & 0x4700u);
  depth = x86p_x87_depth(&in->x87);
  C->top = 0;
  C->depth = 0;
  for (i = depth - 1; i >= 0; i--) {
    long double v = 0.0L;
    x86p_x87_get(&in->x87, i, &v);
    x87_push(C, v);
  }
  for (i = 0; i < 8; i++)
    memcpy(C->xmm[i], in->xmm[i], 16);
}

void x2_engine_callout_from_x86p(const X86pCpu *in, CPU *C) {
  int depth, i;
  C->eax = in->reg[kX86pEax];
  C->ecx = in->reg[kX86pEcx];
  C->edx = in->reg[kX86pEdx];
  C->ebx = in->reg[kX86pEbx];
  C->esp = in->reg[kX86pEsp];
  C->ebp = in->reg[kX86pEbp];
  C->esi = in->reg[kX86pEsi];
  C->edi = in->reg[kX86pEdi];
  C->fcw = in->x87.control;
  C->fsw = (uint32_t)(x86p_x87_status(&in->x87) & 0x4700u);
  depth = x86p_x87_depth(&in->x87);
  C->top = 0;
  C->depth = 0;
  for (i = depth - 1; i >= 0; i--) {
    long double v = 0.0L;
    x86p_x87_get(&in->x87, i, &v);
    x87_push(C, v);
  }
}

void x2_engine_callout_to_x86p(const CPU *C, X86pCpu *out) {
  int i;
  out->reg[kX86pEax] = C->eax;
  out->reg[kX86pEcx] = C->ecx;
  out->reg[kX86pEdx] = C->edx;
  out->reg[kX86pEbx] = C->ebx;
  out->reg[kX86pEsp] = C->esp;
  out->reg[kX86pEbp] = C->ebp;
  out->reg[kX86pEsi] = C->esi;
  out->reg[kX86pEdi] = C->edi;
  x86p_x87_reset(&out->x87);
  out->x87.control = (uint16_t)C->fcw;
  out->x87.status = (uint16_t)(C->fsw & 0x4700u);
  for (i = C->depth - 1; i >= 0; i--)
    x86p_x87_push(&out->x87, C->st[(C->top + i) & 7]);
}
