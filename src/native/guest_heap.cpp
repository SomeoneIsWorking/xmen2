#include "x2_log.h"
/*
 * A heap the guest can hold a pointer to.
 *
 * The host's malloc is not usable for guest allocations: on x86-64 it returns
 * addresses well above 4 GB (measured: 0x55b49f308020), and a guest pointer is
 * 32 bits. Truncating one would produce a pointer that looks fine and is not,
 * which is why imp_MSVCRT_malloc refused rather than casting -- this is what it
 * was refusing on behalf of.
 *
 * SEGREGATED FREE LISTS WITH BOUNDARY TAGS. The first version was a first-fit
 * scan from the arena's base on every malloc and a walk of the whole arena to
 * coalesce on every free, on the reasoning that CRT-level allocation is rare.
 * It is not: the game's operator new reaches here, and on the Dead Zone route
 * the two walks were 3.5% of the process's samples. Now:
 *
 *   - every block starts with an 8-byte header, {magic, size | PREV_FREE};
 *     blocks tile the arena, so the next block is at payload + size;
 *   - a free block also holds its free-list links in its first 8 payload bytes
 *     and its size in its last 4, and the block after it has PREV_FREE set, so
 *     free() finds and merges both neighbours without a walk;
 *   - free blocks are kept in bins by size -- one per 8-byte size up to
 *     SMALL_MAX, one per power of two above -- with a bitmap of the non-empty
 *     ones, so malloc takes the head of an exact bin or of the first larger
 *     non-empty bin, and searches a list only in the request's own power-of-
 *     two bin.
 *
 * Every block carries a magic word. A free() of a pointer this heap did not
 * hand out, or of one already freed, is reported and aborts -- rather than
 * corrupting the arena and surfacing somewhere unrelated much later. A block
 * merged into its free neighbour keeps a MAGIC_FREE header, so freeing it again
 * is still caught as a double free.
 */
#include "guest_heap.h"
#include "guest_memory.h"
#include "x86rt_native.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#define MAGIC_USED 0x55EDB10Cu
#define MAGIC_FREE 0xF2EEB10Cu
#define ALIGN 8u
#define HDR 8u
/* Two links and the footer have to fit in a free block's payload. */
#define MIN_PAYLOAD 16u
#define PREV_FREE 1u
#define SIZE_MASK (~(ALIGN - 1u))
/* Sizes up to this get a bin each; larger ones share a bin per power of two. */
#define SMALL_MAX 1024u
#define SMALL_BINS (SMALL_MAX / ALIGN - 1u)
#define SMALL_MAX_LOG2 10u
#define BINS (SMALL_BINS + 32u - SMALL_MAX_LOG2)
#define BINMAP_WORDS ((BINS + 63u) / 64u)

static uint32_t g_base, g_size;
static uint32_t g_bin[BINS];
static uint64_t g_binmap[BINMAP_WORDS];

/* What the arena is doing, so exhaustion can be reported with its context and
   so a run can be asked how close it came. */
static unsigned long g_live;
static uint32_t g_used, g_highwater;
static int g_exhaustion_reported;

static uint32_t *word(uint32_t address) {
  return (uint32_t *)guest_memory_pointer(address);
}

/* A block, by its header address. */
static uint32_t blk_magic(uint32_t b) { return word(b)[0]; }
static uint32_t blk_size(uint32_t b) { return word(b)[1] & SIZE_MASK; }
static uint32_t blk_prev_free(uint32_t b) { return word(b)[1] & PREV_FREE; }
static uint32_t blk_next(uint32_t b) { return b + HDR + blk_size(b); }

static void blk_set(uint32_t b, uint32_t magic, uint32_t size,
                    uint32_t prev_free) {
  word(b)[0] = magic;
  word(b)[1] = size | prev_free;
}

static void set_prev_free(uint32_t b, uint32_t prev_free) {
  if (b < g_base + g_size) {
    word(b)[1] = (word(b)[1] & ~PREV_FREE) | prev_free;
  }
}

/* A free block's links and footer. */
static uint32_t *free_next(uint32_t b) { return word(b + HDR); }
static uint32_t *free_prev(uint32_t b) { return word(b + HDR + 4u); }
static uint32_t *free_footer(uint32_t b) {
  return word(b + HDR + blk_size(b) - 4u);
}

static unsigned bin_of(uint32_t size) {
  if (size <= SMALL_MAX) {
    return size / ALIGN - 2u;
  }
  return SMALL_BINS + (31u - (unsigned)__builtin_clz(size)) - SMALL_MAX_LOG2;
}

static void bin_insert(uint32_t b) {
  const unsigned bin = bin_of(blk_size(b));
  const uint32_t head = g_bin[bin];
  *free_next(b) = head;
  *free_prev(b) = 0u;
  if (head) {
    *free_prev(head) = b;
  }
  g_bin[bin] = b;
  g_binmap[bin / 64u] |= 1ull << (bin % 64u);
}

static void bin_remove(uint32_t b) {
  const unsigned bin = bin_of(blk_size(b));
  const uint32_t next = *free_next(b);
  const uint32_t prev = *free_prev(b);
  if (prev) {
    *free_next(prev) = next;
  } else {
    g_bin[bin] = next;
  }
  if (next) {
    *free_prev(next) = prev;
  }
  if (!g_bin[bin]) {
    g_binmap[bin / 64u] &= ~(1ull << (bin % 64u));
  }
}

/* Make `b` a free block of `size` and file it. The block before it is never
   free (it would have been merged), and the block after learns that this one
   is. */
static void make_free(uint32_t b, uint32_t size) {
  blk_set(b, MAGIC_FREE, size, 0u);
  *free_footer(b) = size;
  bin_insert(b);
  set_prev_free(blk_next(b), PREV_FREE);
}

/* The first non-empty bin at or above `bin`, or BINS. */
static unsigned first_bin_from(unsigned bin) {
  for (unsigned w = bin / 64u; w < BINMAP_WORDS; w++) {
    uint64_t bits = g_binmap[w];
    if (w == bin / 64u) {
      bits &= ~0ull << (bin % 64u);
    }
    if (bits) {
      return w * 64u + (unsigned)__builtin_ctzll(bits);
    }
  }
  return BINS;
}

/* A free block of at least `n` payload bytes, or 0. Only the request's own
   bin can hold blocks too small for it; every block in a later bin fits. */
static uint32_t find_fit(uint32_t n) {
  const unsigned own = bin_of(n);
  if (own >= SMALL_BINS) {
    for (uint32_t b = g_bin[own]; b; b = *free_next(b)) {
      if (blk_size(b) >= n) {
        return b;
      }
    }
  } else if (g_bin[own]) {
    return g_bin[own];
  }
  const unsigned bin = first_bin_from(own + 1u);
  return bin < BINS ? g_bin[bin] : 0u;
}

int guest_heap_init(uint32_t base, uint32_t size) {
  if (size < 0x10000u || (size & (ALIGN - 1u)))
    return -1;
  if (guest_memory_map_fixed(base, size, PROT_READ | PROT_WRITE) != 0) {
    x2_log_error("guest_heap: could not place a %u-byte arena at "
                 "0x%08x; guest allocations have nowhere to live\n",
                 size, base);
    return -1;
  }
  g_base = base;
  g_size = size;
  memset(g_bin, 0, sizeof g_bin);
  memset(g_binmap, 0, sizeof g_binmap);
  make_free(g_base, size - HDR);
  return 0;
}

static void die(const char *what, uint32_t a) {
  x2_log_error("guest_heap: %s (guest pointer 0x%08x)\n", what, a);
  /* Report before stopping: abort() skips atexit, and without this the stop
     diagnostics are silent on exactly the failures worth reading. Same fix as
     the kernel32 and runtime stop paths. */
  x86_diag_dump();
  abort();
}

static void report_exhausted(uint32_t n) {
  /*
   * EXHAUSTED, and it says so.
   *
   * This used to return 0 in silence with a comment saying the caller
   * reports it. No caller did: the CRT's malloc handed the NULL straight to
   * the guest, the game called its own out-of-memory handler, and that
   * handler's callback had never been installed -- so the run died calling a
   * garbage pointer in a function with no visible connection to memory. The
   * real cause took a trace and four disassemblies to find, and this line
   * would have named it.
   */
  if (g_exhaustion_reported++)
    return;
  x2_log_error("\n*** the guest heap is EXHAUSTED: %u bytes requested, "
               "and the arena is %u bytes at 0x%08x.\n"
               "    %lu allocation(s) live, high-water %u bytes. The "
               "guest gets NULL from malloc, which is honest -- but a "
               "game\n    that asks for more than it was given usually "
               "means the arena is too small, not that the game is "
               "wrong.\n"
               "    Reported once; every later failure is silent.\n",
               n, g_size, g_base, g_live, g_highwater);
}

uint32_t guest_malloc(uint32_t n) {
  if (!g_base)
    die("malloc before the arena was created", 0);
  if (n > g_size) {
    report_exhausted(n);
    return 0;
  }
  n = (n + ALIGN - 1u) & SIZE_MASK;
  if (n < MIN_PAYLOAD)
    n = MIN_PAYLOAD;
  const uint32_t b = find_fit(n);
  if (!b) {
    report_exhausted(n);
    return 0;
  }
  bin_remove(b);
  const uint32_t size = blk_size(b);
  if (size >= n + HDR + MIN_PAYLOAD) {
    blk_set(b, MAGIC_USED, n, 0u);
    make_free(b + HDR + n, size - n - HDR);
  } else {
    blk_set(b, MAGIC_USED, size, 0u);
    set_prev_free(blk_next(b), 0u);
  }
  g_live++;
  g_used += blk_size(b) + HDR;
  if (g_used > g_highwater)
    g_highwater = g_used;
  return b + HDR;
}

void guest_heap_report(void) {
  x2_log_info(
      "  guest heap: %u of %u bytes in use, high-water %u (%.0f%% of the "
      "arena), %lu live allocation(s)\n",
      g_used, g_size, g_highwater,
      g_size ? 100.0 * (double)g_highwater / (double)g_size : 0.0, g_live);
}

void guest_free(uint32_t p) {
  if (!p)
    return; /* free(NULL) is legal */
  if (p < g_base + HDR || p >= g_base + g_size)
    die("free of a pointer outside the guest heap", p);
  uint32_t b = p - HDR;
  if (blk_magic(b) == MAGIC_FREE)
    die("double free", p);
  if (blk_magic(b) != MAGIC_USED)
    die("free of a pointer this heap never "
        "returned (bad header)",
        p);
  uint32_t size = blk_size(b);
  if (g_live)
    g_live--;
  if (g_used >= size + HDR)
    g_used -= size + HDR;
  /* Marked first, so a header merged away below still reads as freed. */
  word(b)[0] = MAGIC_FREE;
  const uint32_t next = blk_next(b);
  if (next < g_base + g_size && blk_magic(next) == MAGIC_FREE) {
    bin_remove(next);
    size += HDR + blk_size(next);
  }
  if (blk_prev_free(b)) {
    const uint32_t prev = b - HDR - *word(b - 4u);
    if (prev < g_base || blk_magic(prev) != MAGIC_FREE || blk_next(prev) != b)
      die("free found a corrupt neighbour before this block", p);
    bin_remove(prev);
    size += HDR + blk_size(prev);
    b = prev;
  }
  make_free(b, size);
}

uint32_t guest_realloc(uint32_t p, uint32_t n) {
  uint32_t q;
  if (!p)
    return guest_malloc(n);
  if (!n) {
    guest_free(p);
    return 0;
  }
  if (p < g_base + HDR || p >= g_base + g_size)
    die("realloc of a pointer outside the guest heap", p);
  if (blk_magic(p - HDR) != MAGIC_USED)
    die("realloc of a pointer this heap never returned", p);
  const uint32_t size = blk_size(p - HDR);
  if (size >= n)
    return p;
  q = guest_malloc(n);
  if (!q)
    return 0;
  memcpy(guest_memory_pointer(q), guest_memory_const_pointer(p), size);
  guest_free(p);
  return q;
}

/* Is this address inside the arena, and what does the block around it look
   like? VirtualQuery needs a guest-side answer; a host one would describe
   mappings the guest cannot see. */
uint32_t guest_heap_base(void) { return g_base; }

int guest_heap_contains(uint32_t a, uint32_t *base, uint32_t *size) {
  if (!g_base || a < g_base || a >= g_base + g_size)
    return 0;
  *base = g_base;
  *size = g_size;
  return 1;
}

/*
 * Is this address inside a block that is still allocated?
 *
 * CONSERVATIVE in one direction on purpose: a block that was freed and handed
 * out again for something else answers "live", so a caller using this to decide
 * that a record is stale keeps it instead of dropping it. Keeping too much is
 * recoverable; dropping a record that is still needed is not.
 *
 * Answers 0 for an address outside the arena, which is NOT the same as "dead"
 * -- ask guest_heap_contains first if the distinction matters.
 *
 * A walk of every block: its callers are diagnostics and setjmp-slot reclaim,
 * which run rarely.
 */
int guest_heap_addr_is_live(uint32_t a) {
  uint32_t b = g_base, end = g_base + g_size;
  if (!g_base || a < g_base || a >= end)
    return 0;
  while (b + HDR <= end) {
    const uint32_t magic = blk_magic(b);
    const uint32_t payload = b + HDR, next = blk_next(b);
    if (magic != MAGIC_USED && magic != MAGIC_FREE)
      break;
    if (a >= payload && a < next)
      return magic == MAGIC_USED;
    if (next <= b)
      break;
    b = next;
  }
  return 0;
}

void guest_heap_stats(uint32_t *used, uint32_t *free_, uint32_t *blocks) {
  uint32_t b = g_base, end = g_base + g_size;
  *used = *free_ = *blocks = 0;
  while (b + HDR <= end) {
    const uint32_t magic = blk_magic(b), next = blk_next(b);
    if (magic == MAGIC_USED)
      *used += blk_size(b);
    else if (magic == MAGIC_FREE)
      *free_ += blk_size(b);
    else
      break;
    (*blocks)++;
    if (next <= b)
      break;
    b = next;
  }
}
