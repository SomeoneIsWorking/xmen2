/*
 * gpu_index_arena.h -- where every index buffer's bytes live.
 *
 * The guest makes hundreds of index buffers, and a frame's draws switch
 * between them all the time. Each one used to be its own SDL_GPU buffer, so
 * about two draws in three rebound the index buffer. Here they are regions of
 * a few large CHUNKS instead. Draws whose regions share a chunk share one
 * bind: the draw starts at the region's first index.
 *
 * A region is never overwritten while a command buffer that has not been
 * submitted may still read it. That is the one hazard SDL_GPU cannot see
 * once uploads stop cycling. The frame's copies are submitted BEFORE the
 * frame's draws (gpu_upload_batch.h), so rewriting a region in place would
 * change the bytes an earlier draw in the same frame reads. Two rules follow:
 *
 *   - the owner RELOCATES a buffer that the open frame has already drawn:
 *     it takes a new region and retires the old one;
 *   - a retired region returns to use only when every command buffer that
 *     was open when it was retired has been submitted.
 *
 * Submission order and SDL's barriers cover everything already submitted.
 *
 * "Open" is measured in EPOCHS. An epoch is one frame command buffer
 * (gpu_frame_epoch in gpu_device.h). Every call takes `closed`, the last
 * epoch whose command buffers have all been submitted.
 *
 * Regions come in power-of-two classes of at least kGpuIndexArenaMinRegion
 * bytes, so every offset is a multiple of that and of either index size.
 * A class smaller than a chunk is carved from a shared chunk. A larger one
 * takes a chunk of its own, which returns to that class's free list and is
 * never shrunk. Nothing here touches a device: the owner creates each chunk
 * when asked, through `make_chunk`, so the policy is testable without a GPU.
 */
#ifndef GPU_INDEX_ARENA_H
#define GPU_INDEX_ARENA_H

#include <stdint.h>

enum {
  kGpuIndexArenaMinRegion = 256,
  kGpuIndexArenaChunkBytes = 8 * 1024 * 1024,
  kGpuIndexArenaMaxChunks = 256
};

typedef struct GpuIndexRegion {
  uint32_t chunk;  /* index of the chunk that holds it */
  uint32_t offset; /* bytes from the chunk's start */
  uint32_t bytes;  /* its capacity: the class size, at least what was asked */
} GpuIndexRegion;

/* Create chunk `chunk` of `bytes` bytes; 0 on failure, which the allocation
   that asked for it reports as its own. */
typedef int (*GpuIndexChunkFn)(void *user, uint32_t chunk, uint32_t bytes);

typedef struct GpuIndexArena GpuIndexArena;

GpuIndexArena *gpu_index_arena_create(GpuIndexChunkFn make_chunk, void *user);
void gpu_index_arena_destroy(GpuIndexArena *arena);

/* A region of at least `bytes` bytes that no open command buffer reads. 0
   when `bytes` is 0 or too large for a class, when the arena has no chunk
   left, or when `make_chunk` fails. */
int gpu_index_arena_alloc(GpuIndexArena *arena, uint32_t bytes, uint64_t closed,
                          GpuIndexRegion *out);

/* Give `region` back. `epoch` is the epoch open now, and never less than the
   one given to the previous call: the region is handed out again once
   `closed` has reached it. */
void gpu_index_arena_retire(GpuIndexArena *arena, GpuIndexRegion region,
                            uint64_t epoch);

typedef struct GpuIndexArenaStats {
  uint32_t chunks;      /* created */
  uint64_t chunk_bytes; /* their total size */
  uint64_t allocs;      /* regions handed out */
  uint64_t reused;      /* ... of which came from a free list */
  uint64_t retired;     /* regions given back */
  uint64_t waiting;     /* ... of which still wait for their epoch */
} GpuIndexArenaStats;

void gpu_index_arena_stats(const GpuIndexArena *arena, GpuIndexArenaStats *out);

#endif
