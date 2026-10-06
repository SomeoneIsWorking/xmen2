/* gpu_index_arena.c -- see gpu_index_arena.h. */
#include "gpu_index_arena.h"

#include <stdlib.h>
#include <string.h>

/* Class k holds regions of kGpuIndexArenaMinRegion << k bytes; the largest
   is 2 GiB, past any buffer SDL_GPU would create. */
enum { kClasses = 24 };

typedef struct FreeList {
  GpuIndexRegion *region;
  size_t count, cap;
} FreeList;

typedef struct Retired {
  GpuIndexRegion region;
  uint64_t epoch;
} Retired;

struct GpuIndexArena {
  GpuIndexChunkFn make_chunk;
  void *user;
  uint32_t chunks;
  /* The shared chunk small classes are carved from, and its bytes carved. */
  int has_carve;
  uint32_t carve_chunk, carve_used;
  FreeList free[kClasses];
  /* Retired regions in the order they were retired, oldest at `head`. */
  Retired *retired;
  size_t head, count, cap;
  GpuIndexArenaStats stats;
};

static int class_of(uint32_t bytes) {
  for (int k = 0; k < kClasses; k++) {
    if (bytes <= ((uint32_t)kGpuIndexArenaMinRegion << k)) {
      return k;
    }
  }
  return -1;
}

static int grow(void **items, size_t *cap, size_t need, size_t item) {
  if (need <= *cap) {
    return 1;
  }
  size_t n = *cap ? *cap * 2u : 64u;
  while (n < need) {
    n *= 2u;
  }
  void *p = realloc(*items, n * item);
  if (!p) {
    return 0;
  }
  *items = p;
  *cap = n;
  return 1;
}

static int push_free(GpuIndexArena *a, GpuIndexRegion region) {
  FreeList *list = &a->free[class_of(region.bytes)];
  if (!grow((void **)&list->region, &list->cap, list->count + 1u,
            sizeof *list->region)) {
    return 0;
  }
  list->region[list->count++] = region;
  return 1;
}

/* Return every retired region whose epoch `closed` has reached. They were
   retired in epoch order, so the first one still waiting ends the scan. */
static void release_closed(GpuIndexArena *a, uint64_t closed) {
  while (a->head < a->count && a->retired[a->head].epoch <= closed) {
    if (!push_free(a, a->retired[a->head].region)) {
      return;
    }
    a->head++;
  }
  if (a->head == a->count) {
    a->head = a->count = 0;
  }
  a->stats.waiting = a->count - a->head;
}

static int new_chunk(GpuIndexArena *a, uint32_t bytes, uint32_t *chunk) {
  if (a->chunks == kGpuIndexArenaMaxChunks ||
      !a->make_chunk(a->user, a->chunks, bytes)) {
    return 0;
  }
  *chunk = a->chunks++;
  a->stats.chunks = a->chunks;
  a->stats.chunk_bytes += bytes;
  return 1;
}

/* The carve chunk's uncarved tail, as the largest regions that fit. */
static void donate_tail(GpuIndexArena *a) {
  uint32_t left = (uint32_t)kGpuIndexArenaChunkBytes - a->carve_used;
  while (left >= (uint32_t)kGpuIndexArenaMinRegion) {
    uint32_t size = (uint32_t)kGpuIndexArenaMinRegion;
    while (size * 2u <= left) {
      size *= 2u;
    }
    const GpuIndexRegion tail = {a->carve_chunk, a->carve_used, size};
    if (!push_free(a, tail)) {
      return;
    }
    a->carve_used += size;
    left -= size;
  }
}

static int carve(GpuIndexArena *a, uint32_t size, GpuIndexRegion *out) {
  if (size >= (uint32_t)kGpuIndexArenaChunkBytes) {
    uint32_t chunk;
    if (!new_chunk(a, size, &chunk)) {
      return 0;
    }
    *out = (GpuIndexRegion){chunk, 0u, size};
    return 1;
  }
  if (!a->has_carve ||
      a->carve_used + size > (uint32_t)kGpuIndexArenaChunkBytes) {
    uint32_t chunk;
    if (!new_chunk(a, (uint32_t)kGpuIndexArenaChunkBytes, &chunk)) {
      return 0;
    }
    if (a->has_carve) {
      donate_tail(a);
    }
    a->has_carve = 1;
    a->carve_chunk = chunk;
    a->carve_used = 0u;
  }
  *out = (GpuIndexRegion){a->carve_chunk, a->carve_used, size};
  a->carve_used += size;
  return 1;
}

GpuIndexArena *gpu_index_arena_create(GpuIndexChunkFn make_chunk, void *user) {
  if (!make_chunk) {
    return NULL;
  }
  GpuIndexArena *a = calloc(1u, sizeof *a);
  if (a) {
    a->make_chunk = make_chunk;
    a->user = user;
  }
  return a;
}

void gpu_index_arena_destroy(GpuIndexArena *a) {
  if (!a) {
    return;
  }
  for (int k = 0; k < kClasses; k++) {
    free(a->free[k].region);
  }
  free(a->retired);
  free(a);
}

int gpu_index_arena_alloc(GpuIndexArena *a, uint32_t bytes, uint64_t closed,
                          GpuIndexRegion *out) {
  const int k = bytes ? class_of(bytes) : -1;
  if (k < 0) {
    return 0;
  }
  release_closed(a, closed);
  FreeList *list = &a->free[k];
  if (list->count) {
    *out = list->region[--list->count];
    a->stats.reused++;
  } else if (!carve(a, (uint32_t)kGpuIndexArenaMinRegion << k, out)) {
    return 0;
  }
  a->stats.allocs++;
  return 1;
}

void gpu_index_arena_retire(GpuIndexArena *a, GpuIndexRegion region,
                            uint64_t epoch) {
  if (!grow((void **)&a->retired, &a->cap, a->count + 1u, sizeof *a->retired)) {
    /* Out of memory: the region is lost to the arena, never handed out
       while it may still be read. */
    return;
  }
  a->retired[a->count++] = (Retired){region, epoch};
  a->stats.retired++;
  a->stats.waiting = a->count - a->head;
}

void gpu_index_arena_stats(const GpuIndexArena *a, GpuIndexArenaStats *out) {
  if (!a) {
    memset(out, 0, sizeof *out);
    return;
  }
  *out = a->stats;
}
