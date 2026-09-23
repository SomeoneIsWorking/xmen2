/*
 * gpu_index_arena.c: regions come from shared chunks in power-of-two
 * classes; a retired region is handed out again only once the epoch it was
 * retired in has closed; a class too large for a shared chunk takes its own;
 * a full chunk's tail is not lost; a chunk that cannot be made fails the
 * allocation that asked for it.
 */
#include "gpu_index_arena.h"

#include <stdio.h>

static int failures;

static void expect(const char *what, uint64_t got, uint64_t want) {
  if (got != want) {
    fprintf(stderr, "FAIL %s: got %llu, want %llu\n", what,
            (unsigned long long)got, (unsigned long long)want);
    failures++;
  }
}

typedef struct Chunks {
  uint32_t made;
  uint32_t bytes[8];
  int refuse; /* make_chunk fails while set */
} Chunks;

static int make_chunk(void *user, uint32_t chunk, uint32_t bytes) {
  Chunks *c = user;
  if (c->refuse || chunk != c->made || chunk >= 8u) {
    return 0;
  }
  c->bytes[c->made++] = bytes;
  return 1;
}

static void classes_share_a_chunk(void) {
  Chunks c = {0};
  GpuIndexArena *a = gpu_index_arena_create(make_chunk, &c);
  GpuIndexRegion r1, r2, r3;
  expect("zero bytes refused", gpu_index_arena_alloc(a, 0u, 0u, &r1), 0);
  expect("no chunk before the first region", c.made, 0);
  expect("6 bytes", gpu_index_arena_alloc(a, 6u, 0u, &r1), 1);
  expect("one shared chunk", c.made, 1);
  expect("its size", c.bytes[0], kGpuIndexArenaChunkBytes);
  expect("6 bytes take the smallest class", r1.bytes, kGpuIndexArenaMinRegion);
  expect("at the start", r1.offset, 0);
  expect("257 bytes", gpu_index_arena_alloc(a, 257u, 0u, &r2), 1);
  expect("257 bytes take the next class", r2.bytes, 512);
  expect("after the first", r2.offset, 256);
  expect("exactly 512 bytes", gpu_index_arena_alloc(a, 512u, 0u, &r3), 1);
  expect("512 fits its class", r3.bytes, 512);
  expect("the same chunk", r3.chunk, r1.chunk);
  expect("still one chunk", c.made, 1);
  gpu_index_arena_destroy(a);
}

static void retired_waits_for_its_epoch(void) {
  Chunks c = {0};
  GpuIndexArena *a = gpu_index_arena_create(make_chunk, &c);
  GpuIndexRegion r1, r2, again;
  gpu_index_arena_alloc(a, 100u, 0u, &r1);
  gpu_index_arena_alloc(a, 100u, 0u, &r2);
  gpu_index_arena_retire(a, r1, 5u);
  gpu_index_arena_retire(a, r2, 7u);
  gpu_index_arena_alloc(a, 100u, 4u, &again);
  expect("epoch 5 is still open at closed 4: a new region", again.offset, 512);
  gpu_index_arena_alloc(a, 100u, 5u, &again);
  expect("closed 5 returns the region retired in 5", again.offset, r1.offset);
  GpuIndexArenaStats st;
  gpu_index_arena_stats(a, &st);
  expect("the one retired in 7 still waits", st.waiting, 1);
  gpu_index_arena_alloc(a, 100u, 6u, &again);
  expect("closed 6 does not return the one retired in 7", again.offset, 768);
  gpu_index_arena_alloc(a, 100u, 7u, &again);
  expect("closed 7 does", again.offset, r2.offset);
  gpu_index_arena_stats(a, &st);
  expect("regions handed out", st.allocs, 6);
  expect("of which reused", st.reused, 2);
  expect("none waits", st.waiting, 0);
  gpu_index_arena_destroy(a);
}

static void a_large_class_takes_its_own_chunk(void) {
  Chunks c = {0};
  GpuIndexArena *a = gpu_index_arena_create(make_chunk, &c);
  GpuIndexRegion big, again;
  expect("one byte past a chunk",
         gpu_index_arena_alloc(a, kGpuIndexArenaChunkBytes + 1u, 0u, &big), 1);
  expect("a chunk of its own", c.made, 1);
  expect("the class above the chunk size", c.bytes[0],
         2u * kGpuIndexArenaChunkBytes);
  expect("at its start", big.offset, 0);
  gpu_index_arena_retire(a, big, 1u);
  expect("the same class again",
         gpu_index_arena_alloc(a, kGpuIndexArenaChunkBytes + 1u, 1u, &again),
         1);
  expect("reuses that chunk", c.made, 1);
  expect("is the same region", again.chunk, big.chunk);
  gpu_index_arena_destroy(a);

  /* Exactly a chunk's size takes its own too, and leaves the shared chunk
     being carved where it was. */
  c = (Chunks){0};
  a = gpu_index_arena_create(make_chunk, &c);
  GpuIndexRegion small;
  gpu_index_arena_alloc(a, 100u, 0u, &small);
  expect("a whole chunk",
         gpu_index_arena_alloc(a, kGpuIndexArenaChunkBytes, 0u, &big), 1);
  expect("in a chunk of its own", big.chunk, 1);
  gpu_index_arena_alloc(a, 100u, 0u, &small);
  expect("the shared chunk carries on", small.offset, 256);
  expect("more than 2 GiB is refused",
         gpu_index_arena_alloc(a, 0x80000001u, 1u, &again), 0);
  gpu_index_arena_destroy(a);
}

static void a_full_chunk_donates_its_tail(void) {
  Chunks c = {0};
  GpuIndexArena *a = gpu_index_arena_create(make_chunk, &c);
  const uint32_t half = kGpuIndexArenaChunkBytes / 2u;
  GpuIndexRegion r;
  gpu_index_arena_alloc(a, half, 0u, &r);
  gpu_index_arena_alloc(a, half / 2u, 0u, &r);
  expect("a second half does not fit the first chunk",
         gpu_index_arena_alloc(a, half, 0u, &r), 1);
  expect("so a second chunk", c.made, 2);
  expect("holds it", r.chunk, 1);
  expect("a quarter", gpu_index_arena_alloc(a, half / 2u, 0u, &r), 1);
  expect("comes from the first chunk's tail", r.chunk, 0);
  expect("where the tail began", r.offset, half + half / 2u);
  expect("no third chunk", c.made, 2);
  gpu_index_arena_destroy(a);
}

static void a_refused_chunk_fails_the_allocation(void) {
  Chunks c = {.refuse = 1};
  GpuIndexArena *a = gpu_index_arena_create(make_chunk, &c);
  GpuIndexRegion r;
  expect("refused", gpu_index_arena_alloc(a, 64u, 0u, &r), 0);
  c.refuse = 0;
  expect("then made", gpu_index_arena_alloc(a, 64u, 0u, &r), 1);
  expect("as chunk 0", r.chunk, 0);
  gpu_index_arena_destroy(a);
}

int main(void) {
  classes_share_a_chunk();
  retired_waits_for_its_epoch();
  a_large_class_takes_its_own_chunk();
  a_full_chunk_donates_its_tail();
  a_refused_chunk_fails_the_allocation();
  if (failures) {
    fprintf(stderr, "%d failure(s)\n", failures);
    return 1;
  }
  printf("gpu_index_arena: all cases passed\n");
  return 0;
}
