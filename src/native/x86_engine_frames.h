#ifndef X2_X86_ENGINE_FRAMES_H
#define X2_X86_ENGINE_FRAMES_H

#include <stdint.h>

/*
 * The engine's per-thread interpreted-call stack.
 *
 * x2_engine_call is re-entered by every guest thread that reaches code the
 * substrate did not recompile, and guest_quantum() now lets those threads
 * interleave inside it (issue #57's movie rendezvous is the case that forced
 * the JIT to yield). The frame stack and its depth are therefore thread-local:
 * a shared one had one thread reading another's return_to and running past its
 * own 0xDEADBEEF entry sentinel into unmapped memory.
 *
 * Frames past ENGINE_FRAMES_MAX count towards the depth but are not stored; the
 * lookups return NULL for them. `deepest` is a cross-thread high-water for the
 * shutdown report only.
 */
#define ENGINE_FRAMES_MAX 64

struct X86pCpu;

typedef struct {
  uint32_t entry;
  uint32_t return_to;
  uint32_t entry_esp;
  const struct X86pCpu *cpu;
} EngineFrame;

void engine_frame_push(uint32_t entry, uint32_t return_to, uint32_t entry_esp,
                       const struct X86pCpu *cpu);
void engine_frame_pop(void);
unsigned long engine_frame_depth(void);
/* Put the depth back after a longjmp unwound the host frames it counted. */
void engine_frame_restore_depth(unsigned long depth);
/* The innermost stored frame, or NULL when the stack is empty or is deeper
   than ENGINE_FRAMES_MAX. */
const EngineFrame *engine_frame_top(void);
/* Frame i (0 = outermost) for the fault dump; NULL once past what is stored. */
const EngineFrame *engine_frame_at(unsigned long i);

unsigned long engine_frame_deepest(void);
void engine_frame_reset_deepest(void);

#endif /* X2_X86_ENGINE_FRAMES_H */
