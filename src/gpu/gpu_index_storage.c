/* gpu_index_storage.c -- see gpu_index_storage.h. */
#include "gpu_index_storage.h"

#ifdef X2_WITH_SDL
#include "../native/x2_log.h"
#include "gpu_internal.h"

#include <string.h>

static GpuIndexArena *g_arena;
static SDL_GPUBuffer *g_chunk[kGpuIndexArenaMaxChunks];
static unsigned long g_relocations;

static int make_chunk(void *user, uint32_t chunk, uint32_t bytes) {
  SDL_GPUBufferCreateInfo ci;
  (void)user;
  memset(&ci, 0, sizeof ci);
  ci.usage = SDL_GPU_BUFFERUSAGE_INDEX;
  ci.size = bytes;
  g_chunk[chunk] = SDL_CreateGPUBuffer(g_gpu, &ci);
  if (!g_chunk[chunk]) {
    x2_log_error("gpu: an index chunk of %u bytes could not be made: %s\n",
                 bytes, SDL_GetError());
    return 0;
  }
  return 1;
}

static int take_region(GpuIndexStorage *s, uint32_t bytes) {
  GpuIndexRegion region;
  if (!g_arena && !(g_arena = gpu_index_arena_create(make_chunk, NULL))) {
    x2_log_error("gpu: no memory for the index arena.\n");
    return 0;
  }
  if (!gpu_index_arena_alloc(g_arena, bytes, gpu_frame_epoch_closed(),
                             &region)) {
    x2_log_error("gpu: no index storage for %u bytes.\n", bytes);
    return 0;
  }
  s->buffer = g_chunk[region.chunk];
  s->region = region;
  s->drawn_epoch = 0;
  return 1;
}

int gpu_index_storage_create(GpuIndexStorage *s, uint32_t bytes) {
  return take_region(s, bytes);
}

int gpu_index_storage_prepare_write(GpuIndexStorage *s) {
  if (s->drawn_epoch <= gpu_frame_epoch_closed()) {
    return 1;
  }
  const GpuIndexStorage old = *s;
  if (!take_region(s, old.region.bytes)) {
    *s = old;
    return 0;
  }
  gpu_index_arena_retire(g_arena, old.region, gpu_frame_epoch());
  g_relocations++;
  return 1;
}

void gpu_index_storage_note_draw(GpuIndexStorage *s) {
  s->drawn_epoch = gpu_frame_epoch();
}

void gpu_index_storage_destroy(GpuIndexStorage *s) {
  if (g_arena && s->buffer) {
    gpu_index_arena_retire(g_arena, s->region, gpu_frame_epoch());
  }
  memset(s, 0, sizeof *s);
}

void gpu_index_storage_shutdown(SDL_GPUDevice *device) {
  GpuIndexArenaStats st;
  gpu_index_arena_stats(g_arena, &st);
  for (uint32_t i = 0; i < st.chunks; i++) {
    SDL_ReleaseGPUBuffer(device, g_chunk[i]);
    g_chunk[i] = NULL;
  }
  gpu_index_arena_destroy(g_arena);
  g_arena = NULL;
  g_relocations = 0;
}

void gpu_index_storage_report(void) {
  GpuIndexArenaStats st;
  gpu_index_arena_stats(g_arena, &st);
  x2_log_info("        index storage: %u chunk(s), %llu byte(s); %llu "
              "region(s) handed out, %llu reused; %lu buffer(s) moved "
              "because the open frame had drawn them; %llu region(s) "
              "waiting for their frame.\n",
              st.chunks, (unsigned long long)st.chunk_bytes,
              (unsigned long long)st.allocs, (unsigned long long)st.reused,
              g_relocations, (unsigned long long)st.waiting);
}
#endif
