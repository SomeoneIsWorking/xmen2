/*
 * guest_heap.c under a seeded random workload, checked against a shadow list
 * of what it handed out: every allocation aligned, inside the arena and
 * disjoint from every other live one; every byte an allocation was given
 * still there when it is freed or moved; the block walk agreeing with the
 * live set; and, when everything is freed, one free block spanning the arena
 * again -- which only holds if every free merged with both neighbours.
 */
#include "guest_heap.h"
#include "guest_memory.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
  HEAP_BASE = 0x31000000u,
  HEAP_SIZE = 0x01000000u,
  HEAP_HDR = 8u,
  SLOTS = 1024u,
  STEPS = 200000u,
};

typedef struct Slot {
  uint32_t address;
  uint32_t bytes;
  uint8_t fill;
} Slot;

static Slot g_slots[SLOTS];
static int failures;
static uint32_t g_rng = 0x2545F491u;

void x86_diag_dump(void) {}

static uint32_t rng(void) {
  g_rng ^= g_rng << 13;
  g_rng ^= g_rng >> 17;
  g_rng ^= g_rng << 5;
  return g_rng;
}

static void fail(const char *what, uint32_t a, uint32_t b) {
  if (failures++ < 10) {
    fprintf(stderr, "FAIL %s (0x%08x, 0x%08x)\n", what, a, b);
  }
}

/* Mostly small objects, some kilobytes, a few large buffers. */
static uint32_t random_bytes(void) {
  const uint32_t kind = rng() % 16u;
  if (kind < 11u) {
    return rng() % 128u;
  }
  if (kind < 15u) {
    return rng() % 4096u;
  }
  return rng() % 65536u;
}

static void fill(const Slot *s) {
  memset(guest_memory_pointer(s->address), s->fill, s->bytes);
}

static void check_contents(const Slot *s) {
  const uint8_t *p = guest_memory_const_pointer(s->address);
  for (uint32_t i = 0; i < s->bytes; i++) {
    if (p[i] != s->fill) {
      fail("contents changed under a live allocation", s->address, i);
      return;
    }
  }
}

static void check_placement(const Slot *s, unsigned self) {
  if (s->address & 7u) {
    fail("misaligned allocation", s->address, 0);
  }
  if (s->address < HEAP_BASE || s->address + s->bytes > HEAP_BASE + HEAP_SIZE) {
    fail("allocation outside the arena", s->address, s->bytes);
  }
  for (unsigned i = 0; i < SLOTS; i++) {
    const Slot *o = &g_slots[i];
    if (i == self || !o->address) {
      continue;
    }
    if (s->address < o->address + o->bytes + 1u &&
        o->address < s->address + s->bytes + 1u) {
      fail("allocations overlap", s->address, o->address);
    }
  }
}

static void step(void) {
  const unsigned i = rng() % SLOTS;
  Slot *s = &g_slots[i];
  const uint32_t op = rng() % 8u;
  if (s->address && op < 3u) {
    check_contents(s);
    guest_free(s->address);
    s->address = 0;
  } else if (s->address && op == 3u) {
    check_contents(s);
    const uint32_t bytes = random_bytes();
    const uint32_t moved = guest_realloc(s->address, bytes ? bytes : 1u);
    if (!moved) {
      fail("realloc failed with room to spare", s->address, bytes);
      return;
    }
    s->address = moved;
    if (bytes < s->bytes) {
      s->bytes = bytes;
    }
    check_contents(s);
    s->bytes = bytes;
    s->fill = (uint8_t)rng();
    fill(s);
    check_placement(s, i);
  } else if (!s->address) {
    s->bytes = random_bytes();
    s->fill = (uint8_t)rng();
    s->address = guest_malloc(s->bytes);
    if (!s->address) {
      fail("malloc failed with room to spare", s->bytes, 0);
      return;
    }
    fill(s);
    check_placement(s, i);
  }
}

static void check_walk(void) {
  uint32_t used, free_, blocks, live = 0, requested = 0;
  guest_heap_stats(&used, &free_, &blocks);
  for (unsigned i = 0; i < SLOTS; i++) {
    if (g_slots[i].address) {
      live++;
      requested += g_slots[i].bytes;
      if (!guest_heap_addr_is_live(g_slots[i].address)) {
        fail("a live allocation reads as freed", g_slots[i].address, 0);
      }
    }
  }
  if (used < requested) {
    fail("the walk sees less in use than is live", used, requested);
  }
  if (used + free_ + blocks * HEAP_HDR != HEAP_SIZE) {
    fail("the blocks do not tile the arena", used + free_, blocks);
  }
  if (blocks < live) {
    fail("the walk sees fewer blocks than live allocations", blocks, live);
  }
}

int main(void) {
  if (guest_heap_init(HEAP_BASE, HEAP_SIZE) != 0) {
    fprintf(stderr, "FAIL: could not create the arena\n");
    return 1;
  }
  for (unsigned n = 0; n < STEPS && !failures; n++) {
    step();
    if (n % 4096u == 0u) {
      check_walk();
    }
  }
  check_walk();
  for (unsigned i = 0; i < SLOTS; i++) {
    if (g_slots[i].address) {
      check_contents(&g_slots[i]);
      guest_free(g_slots[i].address);
      g_slots[i].address = 0;
    }
  }
  uint32_t used, free_, blocks;
  guest_heap_stats(&used, &free_, &blocks);
  if (used != 0u || blocks != 1u || free_ != HEAP_SIZE - HEAP_HDR) {
    fail("everything freed did not merge back into one block", free_, blocks);
  }
  if (guest_malloc(HEAP_SIZE) != 0u) {
    fail("an allocation larger than the arena succeeded", HEAP_SIZE, 0);
  }
  if (!guest_malloc(HEAP_SIZE - HEAP_HDR)) {
    fail("the whole merged arena could not be allocated", HEAP_SIZE, 0);
  }
  if (failures) {
    fprintf(stderr, "%d failure(s)\n", failures);
    return 1;
  }
  printf("guest_heap: ok (%u steps)\n", (unsigned)STEPS);
  return 0;
}
