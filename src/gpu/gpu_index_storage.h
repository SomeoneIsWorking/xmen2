/*
 * gpu_index_storage.h -- an index buffer's bytes, as a region of a shared
 * chunk (gpu_index_arena.h).
 *
 * gpu_draw.c owns the handles and this owns their storage. A draw binds
 * `buffer` from offset 0 and starts `region.offset / index size` indices
 * further in. A buffer the open frame has already drawn is written in a new
 * region, so the earlier draws keep their bytes. Chunks are never cycled, so
 * a bind stays valid across uploads.
 */
#ifndef GPU_INDEX_STORAGE_H
#define GPU_INDEX_STORAGE_H

#ifdef X2_WITH_SDL
#include "gpu_index_arena.h"

#include <SDL3/SDL.h>

typedef struct GpuIndexStorage {
  SDL_GPUBuffer *buffer; /* the chunk; not owned */
  GpuIndexRegion region;
  uint64_t drawn_epoch; /* the frame epoch that last drew it; 0 for none */
} GpuIndexStorage;

/* Storage of at least `bytes` bytes; 0, reported, when there is none. */
int gpu_index_storage_create(GpuIndexStorage *s, uint32_t bytes);

/* Called before every write: moves `s` to a new region when a command buffer
   that has not been submitted drew the one it has. 0, reported, when no
   region is left; `s` then still names its old storage. */
int gpu_index_storage_prepare_write(GpuIndexStorage *s);

/* Called for every draw that reads `s`. */
void gpu_index_storage_note_draw(GpuIndexStorage *s);

void gpu_index_storage_destroy(GpuIndexStorage *s);

/* Release every chunk; the device is going away. */
void gpu_index_storage_shutdown(SDL_GPUDevice *device);

/* One report line: chunks, regions, relocations. */
void gpu_index_storage_report(void);
#endif

#endif
