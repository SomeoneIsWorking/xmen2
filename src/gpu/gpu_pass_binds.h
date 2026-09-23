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
 * Fragment samplers are named by each binding's texture and sampler. No
 * texture is uploaded with cycling, so a texture keeps its storage for its
 * lifetime and the same pair is the same binding. A pipeline bind marks the
 * sets stale inside SDL but keeps the bound samplers, so a kept sampler set
 * survives a new pipeline.
 *
 * A vertex or index buffer is named by its handle AND a serial that changes
 * at every upload: an upload cycles the buffer to new backing storage while
 * the pass is open, and only a fresh bind picks that storage up. Serials are
 * unique across buffers, so a buffer recreated at a freed one's address is new
 * too.
 *
 * Identities are opaque pointers so the decisions are testable without a
 * device; the caller does the binding a `*_changed` call asks for.
 */
#include <stdint.h>

enum { kGpuPassFragmentSamplers = 4 };

typedef struct GpuPassBinds {
  const void *pipeline;
  const void *vertex_buffer; /* slot 0, the only one bound */
  uint64_t vertex_serial;
  const void *index_buffer;
  uint64_t index_serial;
  unsigned index_size;
  /* texture, sampler for each bound slot from 0; `samplers` slots bound */
  const void *sampler_pairs[2 * kGpuPassFragmentSamplers];
  unsigned samplers;
  unsigned long pipelines_kept, vertices_kept, indices_kept, samplers_kept;
} GpuPassBinds;

/* The frame render pass's record. */
GpuPassBinds *gpu_pass_binds(void);

/* A new pass: nothing is bound. */
void gpu_pass_binds_reset(GpuPassBinds *binds);

/* 1, and remembered, when `pipeline` is not the one bound. */
int gpu_pass_binds_pipeline_changed(GpuPassBinds *binds, const void *pipeline);

/* 1, and remembered, when the `count` fragment samplers from slot 0 --
   `pairs` holds texture, sampler for each -- differ from what is bound.
   A longer set is always bound and never remembered. */
int gpu_pass_binds_samplers_changed(GpuPassBinds *binds,
                                    const void *const pairs[], unsigned count);

/* 1, and remembered, when the vertex buffer for slot 0 or its serial
   differs from what is bound. */
int gpu_pass_binds_vertex_changed(GpuPassBinds *binds, const void *buffer,
                                  uint64_t serial);

/* 1, and remembered, when the index buffer, its serial or its element size
   differs from what is bound. */
int gpu_pass_binds_index_changed(GpuPassBinds *binds, const void *buffer,
                                 uint64_t serial, unsigned element_size);

#endif /* GPU_PASS_BINDS_H */
