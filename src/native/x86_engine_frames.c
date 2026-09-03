#include "x86_engine_frames.h"

/*
 * Thread-local: one stack per guest thread. See the header for why a shared one
 * was wrong once the JIT started yielding mid-call.
 */
static __thread EngineFrame t_frame[ENGINE_FRAMES_MAX];
static __thread unsigned long t_depth;

/* A diagnostic high-water only, so a non-atomic max across threads is fine. */
static unsigned long g_deepest;

void engine_frame_push(uint32_t entry, uint32_t return_to, uint32_t entry_esp,
                       const struct X86pCpu *cpu) {
  if (++t_depth > g_deepest)
    g_deepest = t_depth;
  if (t_depth <= ENGINE_FRAMES_MAX) {
    EngineFrame *f = &t_frame[t_depth - 1];
    f->entry = entry;
    f->return_to = return_to;
    f->entry_esp = entry_esp;
    f->cpu = cpu;
  }
}

void engine_frame_pop(void) {
  if (t_depth && t_depth <= ENGINE_FRAMES_MAX)
    t_frame[t_depth - 1].cpu = 0;
  if (t_depth)
    t_depth--;
}

unsigned long engine_frame_depth(void) { return t_depth; }

void engine_frame_restore_depth(unsigned long depth) { t_depth = depth; }

const EngineFrame *engine_frame_top(void) {
  if (!t_depth || t_depth > ENGINE_FRAMES_MAX)
    return 0;
  return &t_frame[t_depth - 1];
}

const EngineFrame *engine_frame_at(unsigned long i) {
  if (i >= t_depth || i >= ENGINE_FRAMES_MAX)
    return 0;
  return &t_frame[i];
}

unsigned long engine_frame_deepest(void) { return g_deepest; }

void engine_frame_reset_deepest(void) { g_deepest = 0; }
