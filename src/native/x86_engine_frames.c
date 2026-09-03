#include "x86_engine_frames.h"

/*
 * Thread-local: one stack per guest thread. See the header for why a shared one
 * was wrong once the JIT started yielding mid-call.
 */
X2_TLS_INTERNAL __thread EngineFrame t_engine_frame[ENGINE_FRAMES_MAX];
X2_TLS_INTERNAL __thread unsigned long t_engine_depth;
X2_TLS_INTERNAL __thread const EngineFrame *t_engine_top;

/* A diagnostic high-water only, so a non-atomic max across threads is fine. */
static unsigned long g_deepest;

void engine_frame_push(uint32_t entry, uint32_t return_to, uint32_t entry_esp,
                       const struct X86pCpu *cpu) {
  if (++t_engine_depth > g_deepest)
    g_deepest = t_engine_depth;
  if (t_engine_depth <= ENGINE_FRAMES_MAX) {
    EngineFrame *f = &t_engine_frame[t_engine_depth - 1];
    f->entry = entry;
    f->return_to = return_to;
    f->entry_esp = entry_esp;
    f->cpu = cpu;
    t_engine_top = f;
  } else {
    t_engine_top = 0;
  }
}

void engine_frame_pop(void) {
  if (t_engine_depth && t_engine_depth <= ENGINE_FRAMES_MAX)
    t_engine_frame[t_engine_depth - 1].cpu = 0;
  if (t_engine_depth) {
    t_engine_depth--;
    t_engine_top = (t_engine_depth && t_engine_depth <= ENGINE_FRAMES_MAX)
                       ? &t_engine_frame[t_engine_depth - 1]
                       : 0;
  }
}

void engine_frame_restore_depth(unsigned long depth) {
  t_engine_depth = depth;
  t_engine_top = (depth && depth <= ENGINE_FRAMES_MAX)
                     ? &t_engine_frame[depth - 1]
                     : 0;
}

const EngineFrame *engine_frame_at(unsigned long i) {
  if (i >= t_engine_depth || i >= ENGINE_FRAMES_MAX)
    return 0;
  return &t_engine_frame[i];
}

unsigned long engine_frame_deepest(void) { return g_deepest; }

void engine_frame_reset_deepest(void) { g_deepest = 0; }
