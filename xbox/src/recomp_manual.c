/**
 * Manual function overrides and ICALL diagnostics
 *
 * This file provides:
 *   - recomp_lookup_manual()  : intercept specific Xbox VAs with hand-written code
 *   - recomp_icall_fail_log() : log when an indirect call target can't be resolved
 *   - ICALL trace ring buffer  : globals used by the RECOMP_ICALL macro
 *
 * The recomp pipeline generates an auto-dispatch table (recomp_lookup) that
 * resolves most function addresses. recomp_lookup_manual() is called FIRST,
 * giving you a chance to override any function with a custom implementation.
 *
 * Common reasons to add manual overrides:
 *   - Trace a function to understand call flow (wrap the generated version)
 *   - Fix a function the lifter translated incorrectly
 *   - Stub out a function that crashes (return early, set eax to a safe value)
 *   - Redirect a function to a native implementation (e.g., skip CRT init)
 *   - Intercept D3D/audio calls for custom rendering or sound
 */

#include <stddef.h>   /* ptrdiff_t */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>   /* getenv, abort */
#include <string.h>   /* memset */

#define MEM32(a) (*(volatile uint32_t *)((uintptr_t)(uint32_t)(a) + g_xbox_mem_offset))

/* ── ICALL trace ring buffer ───────────────────────────────── */

/*
 * These globals are written by the RECOMP_ICALL macro (defined in
 * recomp_types.h) every time an indirect call is dispatched. When a
 * crash occurs, the VEH handler or recomp_icall_fail_log() can dump
 * the last 16 call targets to help you trace what happened.
 *
 * If your recomp_types.h defines these as extern, they must be
 * defined here (or in xbox_memory_layout.c if you use that pattern).
 */
/* Defined by libxbox_kernel (xbox_memory_layout.c). Defining them here too is
   a tentative-definition clash: MSVC merges them, but GCC 10+ defaults to
   -fno-common and rejects it. Declare, do not define. */
#define ICALL_TRACE_SIZE 16   /* must match recomp_types.h */
extern volatile uint32_t g_icall_trace[ICALL_TRACE_SIZE];
extern volatile uint32_t g_icall_trace_idx;
extern volatile uint64_t g_icall_count      ;   /* owned by libxbox_kernel */

typedef void (*recomp_func_t)(void);

/* ── Register state (defined in xbox_memory_layout.c) ──────── */

extern uint32_t g_eax;
extern ptrdiff_t g_xbox_mem_offset;

/* ── Manual function overrides ─────────────────────────────── */

/*
 * Return a function pointer to override the given Xbox VA, or NULL
 * to fall through to the auto-generated dispatch table.
 *
 * This is called on every indirect call (RECOMP_ICALL) and every
 * direct call through the dispatch table, so keep it fast. A chain
 * of if-statements on uint32_t compiles to a simple comparison
 * sequence; for large override tables, consider a sorted array
 * with binary search.
 *
 * Examples of common override patterns:
 *
 *   // Trace wrapper: log entry/exit around the generated function
 *   extern void sub_00012345(void);
 *   static void traced_sub_00012345(void) {
 *       fprintf(stderr, "[TRACE] sub_00012345 entered, eax=0x%08X\n", g_eax);
 *       sub_00012345();
 *       fprintf(stderr, "[TRACE] sub_00012345 returned, eax=0x%08X\n", g_eax);
 *   }
 *
 *   // Stub: skip a function entirely (return 0 in eax)
 *   static void stub_00067890(void) {
 *       g_eax = 0;
 *   }
 *
 *   // Fix: replace a broken lifted function with correct C
 *   static void fixed_sub_000ABCDE(void) {
 *       // Read arguments from stack/registers per calling convention
 *       uint32_t arg1 = g_ecx;
 *       uint32_t arg2 = MEM32(g_esp + 4);
 *       // ... correct implementation ...
 *       g_eax = result;
 *   }
 */

/* ── Native CRT heap override ───────────────────────────────
 *
 * DEBT, not a fix. C056 records a real defect in the recompiled MSVC heap:
 * RtlAllocateHeap eventually returns a block that overlaps the heap's own
 * free-list array, the HEAP_ZERO_MEMORY fill destroys the list heads, and the
 * next allocation walks into .text padding. The heap is provably healthy at
 * the first allocation -- 127 of 128 list heads self-pointing, the 0xEEFFEEFF
 * signature in place -- so this is not missing initialisation, and the cause
 * is still unfound.
 *
 * This replaces the three Rtl entry points with a native allocator so that
 * everything downstream of malloc can be worked on. The recompiled bodies are
 * untouched and still built: XBOX_NATIVE_HEAP=0 runs them instead, which is
 * how the underlying defect stays reproducible.
 *
 * Identified by their ABI and confirmed at runtime:
 *   0x002241E1  RtlAllocateHeap(Heap, Flags, Size)   stdcall, ret 12
 *   0x00222433  RtlFreeHeap(Heap, Flags, Ptr)        stdcall, ret 12, BOOLEAN
 *   0x0022244A  GetProcessHeap()                     returns MEM32(0x731568)
 *
 * The arena lives in guest address space (xbox_HeapAlloc), so every pointer
 * handed back is a real Xbox VA the recompiled code can dereference.
 */

extern uint32_t xbox_HeapAlloc(uint32_t size, uint32_t alignment);
extern uint32_t g_esp;

#define NH_ARENA_SIZE   (24u * 1024u * 1024u)
#define NH_ALIGN        16u
#define NH_MAGIC        0x4E484150u   /* 'NHAP' */

/* Block header, immediately before the pointer handed to the guest.
   16 bytes so the payload keeps 16-byte alignment. */
typedef struct {
    uint32_t magic;
    uint32_t size;      /* payload bytes */
    uint32_t next_free; /* guest VA of the next free block's header, 0 = end */
    uint32_t in_use;
} nh_header;

static uint32_t nh_arena, nh_arena_end, nh_next, nh_free_list;
static uint64_t nh_allocs, nh_frees, nh_bytes, nh_reused, nh_failed;

static nh_header *nh_hdr(uint32_t hdr_va)
{
    return (nh_header *)(uintptr_t)((uintptr_t)hdr_va + g_xbox_mem_offset);
}

static int nh_enabled(void)
{
    static int cached = -1;
    if (cached < 0) {
        const char *e = getenv("XBOX_NATIVE_HEAP");
        cached = (e && e[0] == '0') ? 0 : 1;
    }
    return cached;
}

static int nh_init(void)
{
    if (nh_arena) return 1;
    nh_arena = xbox_HeapAlloc(NH_ARENA_SIZE, 4096);
    if (!nh_arena) {
        fprintf(stderr, "[NHEAP] could not reserve a %u-byte arena; the native "
                        "heap override is INACTIVE and the recompiled heap "
                        "will run instead\n", NH_ARENA_SIZE);
        fflush(stderr);
        return 0;
    }
    nh_arena_end = nh_arena + NH_ARENA_SIZE;
    nh_next = (nh_arena + NH_ALIGN - 1) & ~(NH_ALIGN - 1);
    fprintf(stderr, "[NHEAP] native CRT heap active: arena 0x%08X..0x%08X"
                    " (XBOX_NATIVE_HEAP=0 to use the recompiled heap)\n",
            nh_arena, nh_arena_end);
    fflush(stderr);
    return 1;
}

static uint32_t nh_alloc(uint32_t size, int zero)
{
    uint32_t need, prev = 0, cur;

    if (!nh_init()) return 0;
    if (size == 0) size = 1;
    need = (size + NH_ALIGN - 1) & ~(NH_ALIGN - 1);

    /* First fit over the free list. */
    for (cur = nh_free_list; cur; prev = cur, cur = nh_hdr(cur)->next_free) {
        nh_header *h = nh_hdr(cur);
        if (h->size < need) continue;
        if (prev) nh_hdr(prev)->next_free = h->next_free;
        else      nh_free_list = h->next_free;
        h->next_free = 0;
        h->in_use = 1;
        nh_allocs++; nh_reused++; nh_bytes += size;
        if (zero) memset((void *)((uintptr_t)(cur + sizeof(nh_header))
                                  + g_xbox_mem_offset), 0, h->size);
        return cur + (uint32_t)sizeof(nh_header);
    }

    /* Bump. */
    if (nh_next + sizeof(nh_header) + need > nh_arena_end) {
        nh_failed++;
        fprintf(stderr, "[NHEAP] OUT OF ARENA: request %u, %u of %u bytes used,"
                        " %llu live allocations. Returning NULL, which the"
                        " caller will almost certainly mishandle.\n",
                size, nh_next - nh_arena, NH_ARENA_SIZE,
                (unsigned long long)(nh_allocs - nh_frees));
        fflush(stderr);
        return 0;
    }
    {
        uint32_t hdr_va = nh_next;
        nh_header *h = nh_hdr(hdr_va);
        nh_next = hdr_va + (uint32_t)sizeof(nh_header) + need;
        h->magic = NH_MAGIC; h->size = need; h->next_free = 0; h->in_use = 1;
        nh_allocs++; nh_bytes += size;
        if (zero) memset((void *)((uintptr_t)(hdr_va + sizeof(nh_header))
                                  + g_xbox_mem_offset), 0, need);
        return hdr_va + (uint32_t)sizeof(nh_header);
    }
}

static int nh_free(uint32_t ptr)
{
    uint32_t hdr_va;
    nh_header *h;

    if (!ptr) return 1;                      /* free(NULL) is a no-op */
    if (ptr < nh_arena + sizeof(nh_header) || ptr >= nh_arena_end) {
        /* Not ours. Say so rather than corrupting something: a pointer from
           the recompiled heap reaching here means the override was enabled
           after allocations had already been served. */
        static int warned;
        if (!warned++) {
            fprintf(stderr, "[NHEAP] free of 0x%08X which is OUTSIDE the native"
                            " arena -- ignored, and this should not happen\n", ptr);
            fflush(stderr);
        }
        return 0;
    }
    hdr_va = ptr - (uint32_t)sizeof(nh_header);
    h = nh_hdr(hdr_va);
    if (h->magic != NH_MAGIC) {
        static int warned;
        if (!warned++) {
            fprintf(stderr, "[NHEAP] free of 0x%08X whose header magic is"
                            " 0x%08X, not 'NHAP' -- heap corruption, ignored\n",
                    ptr, h->magic);
            fflush(stderr);
        }
        return 0;
    }
    if (!h->in_use) return 1;                /* double free: tolerate */
    h->in_use = 0;
    h->next_free = nh_free_list;
    nh_free_list = hdr_va;
    nh_frees++;
    return 1;
}

/* These are reached through the linker's --wrap, not recomp_lookup_manual:
   the generated code calls RtlAllocateHeap DIRECTLY as a C function, and the
   manual dispatch hook only covers INDIRECT calls -- the first attempt at
   this override reported "0 allocations, nothing called RtlAllocateHeap".
   --wrap redirects every call site to __wrap_X while keeping the recompiled
   body reachable as __real_X, which is what makes the A/B toggle real. */
extern void __real_sub_002241E1(void);
extern void __real_sub_00222433(void);

/* stdcall(Heap, Flags, Size) -> pointer in eax, ret 12 */
void __wrap_sub_002241E1(void)
{
    if (!nh_enabled()) { __real_sub_002241E1(); return; }
    {
    uint32_t flags = MEM32(g_esp + 8);
    uint32_t size  = MEM32(g_esp + 12);
    g_eax = nh_alloc(size, (flags & 8) != 0);   /* 8 = HEAP_ZERO_MEMORY */
    g_esp += 16;                                /* dummy retaddr + 12 args */
    }
}

/* stdcall(Heap, Flags, Ptr) -> BOOLEAN in eax, ret 12 */
void __wrap_sub_00222433(void)
{
    if (!nh_enabled()) { __real_sub_00222433(); return; }
    {
        uint32_t ptr = MEM32(g_esp + 12);
        g_eax = (uint32_t)nh_free(ptr);
        g_esp += 16;
    }
}

void nh_report(void)
{
    if (!nh_enabled()) {
        fprintf(stderr, "[NHEAP] native heap DISABLED (XBOX_NATIVE_HEAP=0);"
                        " the recompiled MSVC heap ran instead\n");
        return;
    }
    if (!nh_arena) {
        fprintf(stderr, "[NHEAP] native heap never initialised: 0 allocations"
                        " -- nothing called RtlAllocateHeap\n");
        return;
    }
    fprintf(stderr, "[NHEAP] %llu allocs (%llu from the free list), %llu frees,"
                    " %llu live, %u of %u arena bytes used, %llu failures\n",
            (unsigned long long)nh_allocs, (unsigned long long)nh_reused,
            (unsigned long long)nh_frees,
            (unsigned long long)(nh_allocs - nh_frees),
            nh_next - nh_arena, NH_ARENA_SIZE,
            (unsigned long long)nh_failed);
}

recomp_func_t recomp_lookup_manual(uint32_t xbox_va)
{
    if (nh_enabled()) {
        /* See the native CRT heap block above: DEBT bypassing C056, with the
           recompiled bodies still built and reachable via XBOX_NATIVE_HEAP=0. */
        if (xbox_va == 0x002241E1) return __wrap_sub_002241E1;
        if (xbox_va == 0x00222433) return __wrap_sub_00222433;
    }

    /*
     * TODO: Add your overrides here. Examples:
     *
     * if (xbox_va == 0x00012345) return traced_sub_00012345;
     * if (xbox_va == 0x00067890) return stub_00067890;
     * if (xbox_va == 0x000ABCDE) return fixed_sub_000ABCDE;
     */

    (void)xbox_va;
    return (recomp_func_t)0;
}

/* ── ICALL failure logging ─────────────────────────────────── */

/*
 * An indirect call whose target is not in the dispatch table does NOT
 * execute: the ICALL macro restores ESP, sets eax=0 and continues as if
 * the call had returned. That is indistinguishable, in the output, from
 * the call having run and done nothing -- which is exactly how the game's
 * main thread entry (0x00225995) read as "the main thread ran and
 * returned" while it had in fact never been entered.
 *
 * So every miss is recorded, and recomp_icall_report() prints the tally
 * unconditionally at exit -- INCLUDING the zero case, with its
 * denominator. "No unresolved indirect calls" must be a statement the
 * run makes, not the absence of one.
 *
 * Two kinds of miss are counted separately:
 *   - range-skipped: the VA fell in the macro's "garbage" window
 *     (>= 0x00400000, < 0xFE000000) and was never looked up at all.
 *     For this XBE .text ends around 0x00400000, so these are usually
 *     junk function pointers -- but a real target hidden here would be
 *     invisible, hence its own counter.
 *   - unresolved: looked up in manual/auto/kernel dispatch, not found.
 *     These are the discovery-loop candidates (see xbox/seeds.json).
 */

#define ICALL_MISS_MAX 64

static uint32_t g_miss_va[ICALL_MISS_MAX];
static uint64_t g_miss_hits[ICALL_MISS_MAX];
static int      g_miss_kind[ICALL_MISS_MAX];   /* 0 = unresolved, 1 = range-skipped */
static int      g_miss_count;                  /* distinct VAs recorded */
static int      g_icall_selftest_active;       /* suppress the fatal path in the self-test */
static uint64_t g_miss_total;                  /* all misses, incl. overflow */
static uint64_t g_miss_dropped;                /* misses past ICALL_MISS_MAX */

static void icall_miss_record(uint32_t va, int kind)
{
    g_miss_total++;
    for (int i = 0; i < g_miss_count; i++) {
        if (g_miss_va[i] == va && g_miss_kind[i] == kind) {
            g_miss_hits[i]++;
            return;
        }
    }
    if (g_miss_count == ICALL_MISS_MAX) {
        g_miss_dropped++;
        return;
    }
    g_miss_va[g_miss_count]   = va;
    g_miss_hits[g_miss_count] = 1;
    g_miss_kind[g_miss_count] = kind;
    g_miss_count++;

    /* First sighting is loud: with the ring buffer of how we got here. */
    fprintf(stderr, "[ICALL] %s VA 0x%08X (icall #%llu) -- the call did NOT execute\n",
            kind ? "range-skipped" : "UNRESOLVED",
            va, (unsigned long long)g_icall_count);
    fprintf(stderr, "  Recent ICALL targets (oldest first):\n");
    for (int i = 0; i < ICALL_TRACE_SIZE; i++) {
        int idx = (g_icall_trace_idx - ICALL_TRACE_SIZE + i) & (ICALL_TRACE_SIZE - 1);
        if (g_icall_trace[idx])
            fprintf(stderr, "    [%2d] 0x%08X\n", i, g_icall_trace[idx]);
    }
    fflush(stderr);
}

/*
 * Continuing past an unresolvable indirect call is a FALLBACK: the macro
 * restores ESP, sets eax=0 and runs on as though a function had returned 0.
 * On real hardware the console would have jumped to that address. So the
 * game state at that moment is already wrong, and every instruction after it
 * is fiction -- which is how one bad vtable pointer turned into a segfault
 * 200 kernel calls later, in a place with no relation to the cause.
 *
 * Default is therefore to stop at the first one, with the ring buffer as the
 * trail. Set XBOX_ICALL_CONTINUE=1 to survey how many distinct targets a run
 * would hit (the triage mode; its results are a wandering process, not the
 * game's behaviour).
 */
void recomp_icall_report(void);   /* defined below */

static int icall_should_continue(void)
{
    static int cached = -1;
    if (cached < 0) {
        const char *e = getenv("XBOX_ICALL_CONTINUE");
        cached = (e && e[0] == '1') ? 1 : 0;
    }
    return cached;
}

static void icall_miss_fatal(uint32_t va, const char *kind)
{
    if (icall_should_continue() || g_icall_selftest_active)
        return;
    fprintf(stderr,
        "[ICALL] FATAL: %s indirect call to 0x%08X. The original would have\n"
        "        jumped there; we cannot, and continuing would execute code the\n"
        "        game never runs. Stopping here so the cause is the last thing\n"
        "        in this log, not the symptom.\n"
        "        Re-run with XBOX_ICALL_CONTINUE=1 to survey further targets.\n",
        kind, va);
    recomp_icall_report();
    abort();
}

void recomp_icall_fail_log(uint32_t va)
{
    icall_miss_record(va, 0);
    icall_miss_fatal(va, "unresolved");
}

void recomp_icall_range_skip_log(uint32_t va)
{
    icall_miss_record(va, 1);
    icall_miss_fatal(va, "out-of-image");
}

extern int xbox_kernel_call_count(void);

void recomp_icall_report(void)
{
    /* The per-call kernel trace is capped at 200 printed lines. A cap on the
       LOG must not be read as the run's length -- print the real total. */
    fprintf(stderr, "\n[KERNEL] %d kernel calls total"
                    " (the per-call trace above stops at 200)\n",
            xbox_kernel_call_count());

    fprintf(stderr, "[ICALL] %llu indirect calls, %llu did NOT execute"
                    " (%d distinct target%s%s)\n",
            (unsigned long long)g_icall_count,
            (unsigned long long)g_miss_total,
            g_miss_count, g_miss_count == 1 ? "" : "s",
            g_miss_dropped ? ", table full -- more exist" : "");

    for (int i = 0; i < g_miss_count; i++)
        fprintf(stderr, "  0x%08X  x%-6llu  %s\n",
                g_miss_va[i], (unsigned long long)g_miss_hits[i],
                g_miss_kind[i] ? "range-skipped" : "unresolved (seed candidate)");

    if (g_miss_dropped)
        fprintf(stderr, "  ... and %llu further misses beyond the first %d targets\n",
                (unsigned long long)g_miss_dropped, ICALL_MISS_MAX);

    /* Blind spots this tally cannot see, stated so a clean report is not
       mistaken for a clean run: direct (non-indirect) calls into stubbed
       addresses, and any target reached before this file's counters exist. */
    if (g_miss_count == 0)
        fprintf(stderr, "  every indirect target resolved; direct calls to"
                        " stubbed addresses are NOT counted here\n");
    fflush(stderr);
}

/* Self-test: feed a VA that cannot possibly be in any dispatch table and
   assert the counters move. Runs when XBOX_ICALL_SELFTEST=1, so the
   instrument is proven to fire in the shipping binary rather than in a
   test build nobody runs. */
void recomp_icall_selftest(void)
{
    int    before_count = g_miss_count;
    uint64_t before_tot = g_miss_total;

    g_icall_selftest_active = 1;

    recomp_icall_fail_log(0x00123457u);        /* odd, unaligned, not a function */
    recomp_icall_range_skip_log(0x00500000u);  /* inside the skipped window */

    if (g_miss_count != before_count + 2 || g_miss_total != before_tot + 2) {
        fprintf(stderr, "[ICALL] SELFTEST FAILED: miss counters did not move"
                        " (%d -> %d, %llu -> %llu)\n",
                before_count, g_miss_count,
                (unsigned long long)before_tot, (unsigned long long)g_miss_total);
        fflush(stderr);
        g_icall_selftest_active = 0;
        return;
    }

    /* Roll the two synthetic entries back so the real tally stays honest. */
    g_miss_count -= 2;
    g_miss_total -= 2;
    g_icall_selftest_active = 0;
    fprintf(stderr, "[ICALL] SELFTEST passed: both miss paths report.\n");
    fflush(stderr);
}
