#ifndef X2_X86_GUEST_CALL_STACK_H
#define X2_X86_GUEST_CALL_STACK_H

#include <stdint.h>

struct X86pCpu;

/*
 * One title-owned guest call that is live on the host stack. The nodes are
 * intrusive: each x2_engine_call owns its node for exactly as long as its
 * host frame exists. That removes the former fixed-depth shadow array and
 * gives JIT interception, inline native-import dispatch, longjmp recovery,
 * and fault diagnostics one current-call authority.
 */
typedef struct X86GuestCallFrame {
  struct X86GuestCallFrame *previous;
  struct X86pCpu *cpu;
  uint32_t entry;
  uint32_t return_to;
  uint32_t entry_esp;
  unsigned long depth;
} X86GuestCallFrame;

void x86_guest_call_push(X86GuestCallFrame *frame, struct X86pCpu *cpu,
                         uint32_t entry, uint32_t return_to,
                         uint32_t entry_esp);
void x86_guest_call_pop(X86GuestCallFrame *frame);

/* Restore the current frame after host longjmp unwound nested calls. */
void x86_guest_call_restore(X86GuestCallFrame *frame);

const X86GuestCallFrame *x86_guest_call_top(void);
/* Returns the current frame only when it owns this canonical CPU instance. */
const X86GuestCallFrame *x86_guest_call_for_cpu(const struct X86pCpu *cpu);
unsigned long x86_guest_call_depth(void);
unsigned long x86_guest_call_deepest(void);
void x86_guest_call_reset_deepest(void);

#endif /* X2_X86_GUEST_CALL_STACK_H */
