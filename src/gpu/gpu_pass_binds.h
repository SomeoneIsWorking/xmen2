#ifndef GPU_PASS_BINDS_H
#define GPU_PASS_BINDS_H

/*
 * What the frame's render pass has bound, so a draw binds only what differs.
 *
 * SDL 3.4's Vulkan backend marks every descriptor set stale on each pipeline
 * bind, the same pipeline or not, and records every index-buffer bind: a
 * draw that rebinds what the previous draw left paid a descriptor-set fetch
 * and update and a tracked bind for nothing.
 *
 * An index buffer is named by its handle AND a serial that changes at every
 * upload: an upload cycles the buffer to new backing storage while the pass
 * is open, and only a fresh bind picks that storage up. Serials are unique
 * across buffers, so a buffer recreated at a freed one's address is new too.
 *
 * Identities are opaque pointers so the decisions are testable without a
 * device; the caller does the binding a `*_changed` call asks for.
 */
#include <stdint.h>

typedef struct GpuPassBinds {
  const void *pipeline;
  const void *index_buffer;
  uint64_t index_serial;
  unsigned index_size;
  unsigned long pipelines_kept, indices_kept;
} GpuPassBinds;

/* The frame render pass's record. */
GpuPassBinds *gpu_pass_binds(void);

/* A new pass: nothing is bound. */
void gpu_pass_binds_reset(GpuPassBinds *binds);

/* 1, and remembered, when `pipeline` is not the one bound. */
int gpu_pass_binds_pipeline_changed(GpuPassBinds *binds, const void *pipeline);

/* 1, and remembered, when the index buffer, its serial or its element size
   differs from what is bound. */
int gpu_pass_binds_index_changed(GpuPassBinds *binds, const void *buffer,
                                 uint64_t serial, unsigned element_size);

#endif /* GPU_PASS_BINDS_H */
