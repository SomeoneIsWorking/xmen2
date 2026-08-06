/*
 * A heap the guest can hold a pointer to.
 *
 * The host's malloc is not usable for guest allocations: on x86-64 it returns
 * addresses well above 4 GB (measured: 0x55b49f308020), and a guest pointer is
 * 32 bits. Truncating one would produce a pointer that looks fine and is not,
 * which is why imp_MSVCRT_malloc refused rather than casting -- this is what it
 * was refusing on behalf of.
 *
 * Deliberately a plain first-fit allocator with coalescing, not a fast one.
 * The game's own allocators sit on top of this (igMemoryPool and friends are
 * recompiled game code); what reaches here is CRT-level allocation, which is
 * comparatively rare. Correctness and a loud failure matter more than speed,
 * and when it stops mattering the interface is small enough to replace.
 *
 * Every block carries a magic word. A free() of a pointer this heap did not
 * hand out, or of one already freed, is reported and aborts -- rather than
 * corrupting the arena and surfacing somewhere unrelated much later.
 */
#include "guest_heap.h"
#include "x86rt_native.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#define MAGIC_USED 0x55EDB10Cu
#define MAGIC_FREE 0xF2EEB10Cu
#define ALIGN      8u

typedef struct Blk {
    uint32_t magic;
    uint32_t size;        /* payload bytes, not counting this header */
} Blk;

static uint32_t g_base, g_size;

/* What the arena is doing, so exhaustion can be reported with its context and
   so a run can be asked how close it came. */
static unsigned long g_live;
static uint32_t g_used, g_highwater;

#define HDR   ((uint32_t)sizeof(Blk))
#define BLK(a) ((volatile Blk *)(uintptr_t)(a))

int guest_heap_init(uint32_t base, uint32_t size)
{
    void *p;
    if (size < 0x10000u) return -1;
    p = mmap((void *)(uintptr_t)base, size, PROT_READ | PROT_WRITE,
             MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE | MAP_NORESERVE,
             -1, 0);
    if (p == MAP_FAILED || (uintptr_t)p != (uintptr_t)base) {
        fprintf(stderr, "guest_heap: could not place a %u-byte arena at "
                        "0x%08x; guest allocations have nowhere to live\n",
                size, base);
        if (p != MAP_FAILED) munmap(p, size);
        return -1;
    }
    g_base = base;
    g_size = size;
    BLK(g_base)->magic = MAGIC_FREE;
    BLK(g_base)->size = size - HDR;
    return 0;
}

static void die(const char *what, uint32_t a)
{
    fprintf(stderr, "guest_heap: %s (guest pointer 0x%08x)\n", what, a);
    /* Report before stopping: abort() skips atexit, and without this the
       reached set and the ring are silent on exactly the failures worth
       reading. Same fix as the kernel32 and runtime stop paths. */
    x86_diag_dump();
    abort();
}

/* Merge each free block with the free block that follows it. Done on free()
   rather than on malloc() so a long-running allocation pattern cannot leave the
   arena permanently shredded. */
static void coalesce(void)
{
    uint32_t a = g_base, end = g_base + g_size;
    while (a + HDR <= end) {
        volatile Blk *b = BLK(a);
        uint32_t next = a + HDR + b->size;
        if (b->magic == MAGIC_FREE && next + HDR <= end
            && BLK(next)->magic == MAGIC_FREE) {
            b->size += HDR + BLK(next)->size;
            continue;                       /* try merging the one after too */
        }
        if (next <= a) break;               /* corrupt size: stop, do not spin */
        a = next;
    }
}

uint32_t guest_malloc(uint32_t n)
{
    uint32_t a = g_base, end;
    if (!g_base) die("malloc before the arena was created", 0);
    if (!n) n = 1;
    n = (n + ALIGN - 1u) & ~(ALIGN - 1u);
    end = g_base + g_size;
    while (a + HDR <= end) {
        volatile Blk *b = BLK(a);
        uint32_t next = a + HDR + b->size;
        if (b->magic == MAGIC_FREE && b->size >= n) {
            if (b->size >= n + HDR + ALIGN) {        /* split */
                uint32_t rest = a + HDR + n;
                BLK(rest)->magic = MAGIC_FREE;
                BLK(rest)->size = b->size - n - HDR;
                b->size = n;
            }
            b->magic = MAGIC_USED;
            g_live++;
            g_used += b->size + HDR;
            if (g_used > g_highwater) g_highwater = g_used;
            return a + HDR;
        }
        if (next <= a) break;
        a = next;
    }
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
    {
        static int said;
        if (!said++)
            fprintf(stderr,
                    "\n*** the guest heap is EXHAUSTED: %u bytes requested, "
                    "and the arena is %u bytes at 0x%08x.\n"
                    "    %lu allocation(s) live, high-water %u bytes. The "
                    "guest gets NULL from malloc, which is honest -- but a "
                    "game\n    that asks for more than it was given usually "
                    "means the arena is too small, not that the game is "
                    "wrong.\n"
                    "    Reported once; every later failure is silent.\n",
                    n, g_size, g_base, g_live, g_highwater);
        fflush(stderr);
    }
    return 0;
}

void guest_heap_report(void)
{
    printf("  guest heap: %u of %u bytes in use, high-water %u (%.0f%% of the "
           "arena), %lu live allocation(s)\n",
           g_used, g_size, g_highwater,
           g_size ? 100.0 * (double)g_highwater / (double)g_size : 0.0, g_live);
}

void guest_free(uint32_t p)
{
    volatile Blk *b;
    if (p) {
        volatile Blk *fb = BLK(p - HDR);
        if (fb->magic == MAGIC_USED) {
            if (g_live) g_live--;
            if (g_used >= fb->size + HDR) g_used -= fb->size + HDR;
        }
    }
    if (!p) return;                                  /* free(NULL) is legal */
    if (p < g_base + HDR || p >= g_base + g_size)
        die("free of a pointer outside the guest heap", p);
    b = BLK(p - HDR);
    if (b->magic == MAGIC_FREE) die("double free", p);
    if (b->magic != MAGIC_USED) die("free of a pointer this heap never "
                                    "returned (bad header)", p);
    b->magic = MAGIC_FREE;
    coalesce();
}

uint32_t guest_realloc(uint32_t p, uint32_t n)
{
    uint32_t q;
    volatile Blk *b;
    if (!p) return guest_malloc(n);
    if (!n) { guest_free(p); return 0; }
    if (p < g_base + HDR || p >= g_base + g_size)
        die("realloc of a pointer outside the guest heap", p);
    b = BLK(p - HDR);
    if (b->magic != MAGIC_USED)
        die("realloc of a pointer this heap never returned", p);
    if (b->size >= n) return p;
    q = guest_malloc(n);
    if (!q) return 0;
    memcpy((void *)(uintptr_t)q, (const void *)(uintptr_t)p, b->size);
    guest_free(p);
    return q;
}

/* Is this address inside the arena, and what does the block around it look
   like? VirtualQuery needs a guest-side answer; a host one would describe
   mappings the guest cannot see. */
uint32_t guest_heap_base(void) { return g_base; }

int guest_heap_contains(uint32_t a, uint32_t *base, uint32_t *size)
{
    if (!g_base || a < g_base || a >= g_base + g_size) return 0;
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
 * Answers 0 for an address outside the arena, which is NOT the same as "dead" --
 * ask guest_heap_contains first if the distinction matters.
 */
int guest_heap_addr_is_live(uint32_t a)
{
    uint32_t p = g_base, end = g_base + g_size;
    if (!g_base || a < g_base || a >= end) return 0;
    while (p + HDR <= end) {
        volatile Blk *b = BLK(p);
        uint32_t payload = p + HDR, next = payload + b->size;
        if (b->magic != MAGIC_USED && b->magic != MAGIC_FREE) break;
        if (a >= payload && a < next) return b->magic == MAGIC_USED;
        if (next <= p) break;
        p = next;
    }
    return 0;
}

void guest_heap_stats(uint32_t *used, uint32_t *free_, uint32_t *blocks)
{
    uint32_t a = g_base, end = g_base + g_size;
    *used = *free_ = *blocks = 0;
    while (a + HDR <= end) {
        volatile Blk *b = BLK(a);
        uint32_t next = a + HDR + b->size;
        if (b->magic == MAGIC_USED) *used += b->size;
        else if (b->magic == MAGIC_FREE) *free_ += b->size;
        else break;
        (*blocks)++;
        if (next <= a) break;
        a = next;
    }
}
