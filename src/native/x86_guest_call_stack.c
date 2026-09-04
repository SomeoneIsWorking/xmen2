#include "x86_guest_call_stack.h"

#include <stdlib.h>

#if defined(__GNUC__) || defined(__clang__)
#define X2_TLS_INTERNAL                                                        \
  __attribute__((visibility("hidden"), tls_model("initial-exec")))
#else
#define X2_TLS_INTERNAL
#endif

static X2_TLS_INTERNAL __thread X86GuestCallFrame *t_top;
static unsigned long g_deepest;

void x86_guest_call_push(X86GuestCallFrame *frame, struct X86pCpu *cpu,
                         uint32_t entry, uint32_t return_to,
                         uint32_t entry_esp) {
  frame->previous = t_top;
  frame->cpu = cpu;
  frame->entry = entry;
  frame->return_to = return_to;
  frame->entry_esp = entry_esp;
  frame->depth = t_top ? t_top->depth + 1u : 1u;
  t_top = frame;
  if (frame->depth > g_deepest)
    g_deepest = frame->depth;
}

void x86_guest_call_pop(X86GuestCallFrame *frame) {
  if (t_top != frame)
    abort();
  t_top = frame->previous;
}

void x86_guest_call_restore(X86GuestCallFrame *frame) { t_top = frame; }

const X86GuestCallFrame *x86_guest_call_top(void) { return t_top; }

const X86GuestCallFrame *x86_guest_call_for_cpu(const struct X86pCpu *cpu) {
  return t_top && t_top->cpu == cpu ? t_top : NULL;
}

unsigned long x86_guest_call_depth(void) { return t_top ? t_top->depth : 0u; }

unsigned long x86_guest_call_deepest(void) { return g_deepest; }

void x86_guest_call_reset_deepest(void) { g_deepest = 0; }
