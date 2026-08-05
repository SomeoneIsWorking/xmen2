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

/* dladdr, used to name recompiled frames, is a GNU extension. */
#ifndef _WIN32
#define _GNU_SOURCE
#endif

#include <stddef.h>   /* ptrdiff_t */
#include <stdio.h>
#ifndef _WIN32
#include <dlfcn.h>
#endif
#include <stdint.h>
#include <stdlib.h>   /* getenv, abort */
#include <string.h>   /* memset, memcpy */

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

/* Generated alongside the dispatch table: the start VA of the function
   containing an address, or 0 when the address is below every function. This
   file is not a consumer of recomp_types.h, so it declares what it uses. */
extern uint32_t recomp_enclosing_func(uint32_t xbox_va);
const char *recomp_site_str(uint32_t site);

extern uint32_t g_eax;
extern uint32_t g_ecx;
extern uint32_t g_esp;
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

/* ── Callee-saved register contract check ───────────────────
 *
 * In the MSVC/x86 ABI ebx, esi, edi and ebp are CALLEE-SAVED: a function must
 * leave them as it found them. The recompiled code models registers as
 * globals, so a callee that fails to restore one silently corrupts its
 * caller -- and the caller then uses a `this` pointer, a loop counter or a
 * saved base that belongs to nobody. That is invisible until something
 * dereferences it, by which point the trail is cold.
 *
 * So every indirect call checks the contract and reports the FIRST violation
 * per target, with which register and what it became. Costs four compares per
 * indirect call; set XBOX_ABICHECK=0 to turn it off.
 */

#define ABI_MISS_MAX 32
static uint32_t abi_va[ABI_MISS_MAX];
static int      abi_count;
static uint64_t abi_violations, abi_checked;

int recomp_abicheck_enabled(void)
{
    static int cached = -1;
    if (cached < 0) {
        const char *e = getenv("XBOX_ABICHECK");
        cached = (e && e[0] == '0') ? 0 : 1;
    }
    return cached;
}

/* __SEH_prolog and __SEH_epilog exist precisely to rewrite the caller's frame
   -- establishing ebp and restoring ebx/esi/edi is their job, not a contract
   breach. Excluding them is not weakening the check: including them buries
   the real violations under hundreds of expected ones. Addresses are this
   title's, as reported by the lifter: "SEH helpers: __SEH_prolog 0x003D62F4,
   __SEH_epilog 0x003D632F". */
#define SEH_PROLOG_VA 0x003D62F4u
#define SEH_EPILOG_VA 0x003D632Fu

/* _aulldvrm: the CRT's 64-bit divide-with-remainder. It RETURNS the remainder
   in ecx:ebx -- read its epilogue, which ends `edx = ebx; ebx = ecx; ecx =
   eax; eax = esi;` before a single `pop esi`. Clobbering ebx is its calling
   convention, not a contract breach, and it starts at 0x003DC1B0 with `push
   esi` (0x003DC1AF is 0xCC padding), so no `push ebx` is missing. Verified
   against the bytes rather than assumed. */
#define AULLDVRM_VA   0x003DC1B0u

void recomp_abicheck_report_violation(uint32_t va, uint32_t site, const char *reg,
                                      uint32_t before, uint32_t after)
{
    if (va == SEH_PROLOG_VA || va == SEH_EPILOG_VA ||
        va == AULLDVRM_VA) return;

    abi_violations++;
    for (int i = 0; i < abi_count; i++)
        if (abi_va[i] == va) return;
    if (abi_count < ABI_MISS_MAX) abi_va[abi_count++] = va;

    fprintf(stderr, "[ABI] 0x%08X did not restore %s: 0x%08X -> 0x%08X."
                    " It is callee-saved, so the CALLER is now corrupt."
                    " Called from %s.\n",
            va, reg, before, after, recomp_site_str(site));
    fflush(stderr);
}

void recomp_abicheck_count(void) { abi_checked++; }

/* ── Simulated stack bounds ─────────────────────────────────
 *
 * The guest stack is a known, fixed region. ESP leaving it means the
 * simulated stack went out of balance -- too many pops, a `ret N` with the
 * wrong N, a callee that did not clean what it pushed. By the time that
 * surfaces (a POP reading heap memory as a saved register) the cause is long
 * gone, so report the FIRST departure with the call that was in flight.
 */
#define STACK_LO  0x00780000u
#define STACK_HI  0x00F80000u

#define ESP_MISS_MAX 32
static uint64_t esp_violations;
static uint32_t esp_va[ESP_MISS_MAX];
static uint64_t esp_hits[ESP_MISS_MAX];
static int      esp_count;

void recomp_esp_check(uint32_t va, uint32_t site)
{
    if (g_esp >= STACK_LO && g_esp < STACK_HI) return;
    esp_violations++;
    for (int i = 0; i < esp_count; i++)
        if (esp_va[i] == va) { esp_hits[i]++; return; }
    if (esp_count >= ESP_MISS_MAX) return;
    esp_va[esp_count] = va;
    esp_hits[esp_count] = 1;
    esp_count++;

    fprintf(stderr, "[ESP] esp = 0x%08X is OUTSIDE the guest stack"
                    " (0x%08X..0x%08X) after the call to 0x%08X from %s."
                    " The simulated stack is out of balance; every POP from"
                    " here reads memory that is not a saved register.\n",
            g_esp, STACK_LO, STACK_HI, va, recomp_site_str(site));
    fflush(stderr);
}

void recomp_esp_report(void)
{
    if (!esp_violations) {
        fprintf(stderr, "[ESP] esp stayed inside the guest stack for every"
                        " checked call\n");
        return;
    }
    fprintf(stderr, "[ESP] %llu calls returned with esp outside the guest"
                    " stack, across %d distinct targets:\n",
            (unsigned long long)esp_violations, esp_count);
    for (int i = 0; i < esp_count; i++)
        fprintf(stderr, "  0x%08X  x%llu\n", esp_va[i],
                (unsigned long long)esp_hits[i]);
}

void recomp_abicheck_report(void)
{
    if (!recomp_abicheck_enabled()) {
        fprintf(stderr, "[ABI] callee-saved checking DISABLED"
                        " (XBOX_ABICHECK=0)\n");
        return;
    }
    if (!abi_violations) {
        fprintf(stderr, "[ABI] %llu calls checked, every one restored"
                        " ebx/esi/edi/ebp. Exempt by convention, not by"
                        " weakening: the two SEH helpers (rewriting the frame"
                        " is their job) and _aulldvrm (returns its remainder"
                        " in ecx:ebx)\n",
                (unsigned long long)abi_checked);
        return;
    }
    fprintf(stderr, "[ABI] %llu violations across %d distinct targets"
                    " (%llu indirect calls checked)\n",
            (unsigned long long)abi_violations, abi_count,
            (unsigned long long)abi_checked);
}

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
extern void __real_sub_00224B50(void);

static int nh_owns(uint32_t ptr)
{
    return ptr >= nh_arena + sizeof(nh_header) && ptr < nh_arena_end;
}

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

/* stdcall(Heap, Flags, Ptr, Size) -> pointer in eax, ret 16.
 *
 * Closing the gap C057 recorded: without this, the CRT's realloc reaches the
 * RECOMPILED heap with a pointer from the native arena, reads the native
 * block header as its own, and corrupts it with nothing to show for it. */
void __wrap_sub_00224B50(void)
{
    if (!nh_enabled()) { __real_sub_00224B50(); return; }
    {
        uint32_t ptr  = MEM32(g_esp + 12);
        uint32_t size = MEM32(g_esp + 16);
        uint32_t out  = 0;

        if (!ptr) {
            out = nh_alloc(size, 0);
        } else if (nh_owns(ptr)) {
            uint32_t old = nh_hdr(ptr - (uint32_t)sizeof(nh_header))->size;
            if (old >= size) {
                out = ptr;                    /* shrink in place */
            } else {
                out = nh_alloc(size, 0);
                if (out) {
                    memcpy((void *)((uintptr_t)out + g_xbox_mem_offset),
                           (void *)((uintptr_t)ptr + g_xbox_mem_offset), old);
                    nh_free(ptr);
                }
            }
        } else {
            /* Not ours and not NULL: refuse rather than hand it to either
               allocator, and say why -- this would be silent corruption. */
            static int warned;
            if (!warned++) {
                fprintf(stderr, "[NHEAP] realloc of 0x%08X, which is not in the"
                                " native arena -- refused. A pointer predating"
                                " the override has reached realloc.\n", ptr);
                fflush(stderr);
            }
            out = 0;
        }
        g_eax = out;
        g_esp += 20;                           /* dummy retaddr + 16 args */
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
        if (xbox_va == 0x00224B50) return __wrap_sub_00224B50;
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
static int      g_miss_kind[ICALL_MISS_MAX];   /* 0 = unresolved call, 1 = range-skipped,
                                                  2 = tail jump through an unenumerated
                                                  switch table (never a seed) */
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

    /* First sighting is loud: with the ring buffer of how we got here.
       The KIND is part of the line, because tooling greps this: the discovery
       loop seeds whatever "UNRESOLVED VA" names, and a tail jump through an
       unenumerated switch table names a jump-table target in the middle of a
       function. Printing that as an unresolved CALL is what got three
       mid-function fragments seeded as functions. */
    fprintf(stderr, "[ICALL] %s VA 0x%08X (icall #%llu) -- the %s did NOT execute\n",
            kind == 1 ? "range-skipped"
                      : (kind == 2 ? "UNRESOLVED-TAIL-JUMP" : "UNRESOLVED"),
            va, (unsigned long long)g_icall_count,
            kind == 2 ? "jump" : "call");
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
void nh_report(void);             /* defined below */
extern void recomp_stub_report(void);  /* generated stub tally */
void recomp_abicheck_report(void);     /* defined below */
void recomp_esp_report(void);          /* defined below */

static int icall_should_continue(void)
{
    static int cached = -1;
    if (cached < 0) {
        const char *e = getenv("XBOX_ICALL_CONTINUE");
        cached = (e && e[0] == '1') ? 1 : 0;
    }
    return cached;
}

/*
 * Print the recompiled call stack, resolving each return address to the
 * sub_XXXXXXXX whose name carries the original Xbox VA (needs -rdynamic).
 *
 * The crash handler has had this since it stopped filtering for a Windows
 * image base that never matches on Linux. The INDIRECT-CALL failure path did
 * not, so the one question that matters there -- who was about to call this
 * address -- had no answer in the log, and a call through a NULL pointer named
 * only itself. Both paths share it now.
 */
void recomp_print_native_stack(void)
{
#ifndef _WIN32
    uintptr_t *sp = (uintptr_t *)__builtin_frame_address(0);
    Dl_info info;
    int shown = 0, scanned = 0;

    fprintf(stderr, "  Recompiled call stack (nearest symbol per return address):\n");
    for (int i = 0; i < 256; i++, scanned++) {
        if (!sp[i]) continue;
        if (dladdr((void *)sp[i], &info) && info.dli_sname) {
            fprintf(stderr, "    [%d] %s +0x%lX\n", i, info.dli_sname,
                    (unsigned long)(sp[i] - (uintptr_t)info.dli_saddr));
            if (++shown >= 24) break;
        }
    }
    /* The denominator, so "no frames" cannot be read as "never looked". */
    fprintf(stderr, "    %d frame(s) resolved from %d stack slots scanned\n",
            shown, scanned);
    fflush(stderr);
#endif
}

/*
 * Render a guest call-site VA as "0x........ (sub_XXXXXXXX+0xNN)".
 *
 * The enclosing function comes from the generated dispatch table, which is
 * sorted by VA, so this is the same data the run dispatches through -- not a
 * second list that can drift out of step with it.
 *
 * A site of 0 means the caller had none to give (a hand-written call site, or
 * the self-test). Say that instead of printing "0x00000000", which reads as a
 * real address and sends the reader looking for an instruction there.
 */
const char *recomp_site_str(uint32_t site)
{
    static char buf[64];
    if (!site) {
        snprintf(buf, sizeof buf, "an unrecorded call site");
        return buf;
    }
    uint32_t owner = recomp_enclosing_func(site);
    if (owner)
        snprintf(buf, sizeof buf, "guest 0x%08X (sub_%08X+0x%X)",
                 site, owner, site - owner);
    else
        snprintf(buf, sizeof buf, "guest 0x%08X (no known enclosing function)",
                 site);
    return buf;
}

static void icall_miss_fatal(uint32_t va, uint32_t site, const char *kind)
{
    if (icall_should_continue() || g_icall_selftest_active)
        return;
    fprintf(stderr,
        "[ICALL] FATAL: %s indirect call to 0x%08X, from %s.\n"
        "        The original would have\n"
        "        jumped there; we cannot, and continuing would execute code the\n"
        "        game never runs. Stopping here so the cause is the last thing\n"
        "        in this log, not the symptom.\n"
        "        Re-run with XBOX_ICALL_CONTINUE=1 to survey further targets.\n",
        kind, va, recomp_site_str(site));
    recomp_print_native_stack();
    recomp_icall_report();
    nh_report();
    recomp_stub_report();
    recomp_abicheck_report();
    recomp_esp_report();
    abort();
}

void recomp_icall_fail_log(uint32_t va, uint32_t site)
{
    icall_miss_record(va, 0);
    icall_miss_fatal(va, site, "unresolved");
}

void recomp_icall_range_skip_log(uint32_t va, uint32_t site)
{
    icall_miss_record(va, 1);
    icall_miss_fatal(va, site, "out-of-image");
}

/*
 * A tail JUMP that could not be resolved -- not a call.
 *
 * This used to log through recomp_icall_fail_log, which made the two
 * indistinguishable, and that cost real damage. RECOMP_ITAIL is what a jump
 * table falls back to when the recompiler could not enumerate its entries, so
 * the address it reports is a jump-table TARGET: an instruction in the middle
 * of the function that was dispatching. memcpy's unrolled copy tail is full of
 * them (0x003D5B44 = `mov eax,[esi+ecx*4+0x10]`).
 *
 * Reported as "unresolved indirect call", those addresses look exactly like
 * functions the detector missed, so the discovery loop seeded three of them.
 * Each became a "function" with no prologue: the dispatch resolved, the
 * fragment ran on the caller's frame, and the boot continued with a corrupted
 * state -- a name lookup was handed a `this` pointing into the stack. The
 * symptom moved and the port got further while being more wrong.
 *
 * So say which one it is. A tail-jump miss is a TRANSLATOR gap (an
 * unenumerated switch table), never a missing function, and must never be
 * seeded.
 */
void recomp_itail_fail_log(uint32_t va, uint32_t site)
{
    icall_miss_record(va, 2);
    fprintf(stderr,
        "[ICALL] 0x%08X is a TAIL-JUMP miss, not a missing function.\n"
        "        This is a JUMP, not a call: it is the fallback of a switch\n"
        "        table the recompiler could not enumerate, so this address is\n"
        "        a jump-table TARGET inside some function, not a function.\n"
        "        DO NOT SEED IT. Seeding builds a function with no prologue\n"
        "        and the run continues corrupted. Fix the table instead: find\n"
        "        the 'indirect tail jmp' fallback in the caller's generated C\n"
        "        and see _analyze_switch_table in the lifter.\n"
        "        The jump is at %s.\n", va, recomp_site_str(site));
    fflush(stderr);
    icall_miss_fatal(va, site, "unresolved tail-jump");
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
                g_miss_kind[i] == 1 ? "range-skipped (garbage pointer)"
                : g_miss_kind[i] == 2
                    ? "unenumerated switch table -- NOT a seed candidate"
                    : "unresolved (seed candidate)");

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

    recomp_icall_fail_log(0x00123457u, SEH_PROLOG_VA + 4);  /* odd, unaligned, not a function */
    recomp_icall_range_skip_log(0x00500000u, 0);             /* inside the skipped window */

    /* The call-site attribution is an instrument too, so prove it against
       BOTH classes rather than trusting that it looks right. A VA four bytes
       into a known function must resolve to that function; a VA below every
       function must resolve to nothing. Getting only the positive right would
       still pass with a lookup that returns the nearest entry unconditionally,
       and that lookup names a wrong function for every unmapped site. */
    uint32_t owner = recomp_enclosing_func(SEH_PROLOG_VA + 4);
    uint32_t none  = recomp_enclosing_func(1u);
    if (owner != SEH_PROLOG_VA || none != 0) {
        fprintf(stderr, "[ICALL] SELFTEST FAILED: call-site attribution is"
                        " wrong. Inside 0x%08X resolved to 0x%08X (want"
                        " 0x%08X); below the image resolved to 0x%08X"
                        " (want 0). Every 'called from' in this log is"
                        " unreliable.\n",
                SEH_PROLOG_VA, owner, SEH_PROLOG_VA, none);
        fflush(stderr);
        g_icall_selftest_active = 0;
        return;
    }

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
    fprintf(stderr, "[ICALL] SELFTEST passed: both miss paths report, and"
                    " call-site attribution is right in both directions"
                    " (inside a function, and below every function).\n");
    fflush(stderr);
}

/* ── Indirect-call watch ────────────────────────────────────
 *
 * The ABI and ESP checks answer "did this call break the contract". They do
 * not answer the question that actually blocks an investigation: WHAT did
 * this call receive, and what did it hand back. Line breakpoints cannot
 * answer it either -- the generated C is built at -O2, statements are
 * reordered across labels, and a breakpoint on the line after a call fires
 * before the call in one block and three times in another. Measured: a
 * breakpoint on the `if` following an icall reported the CALLER's register,
 * and one on a call line fired three times for a single execution.
 *
 * So the watch lives where the call really happens -- inside the ICALL
 * macros -- and prints target, `this`, the first four stdcall arguments off
 * the guest stack, and the returned eax.
 *
 *     XBOX_ICALL_WATCH=0x0027BEF0,0x0027A9B0    (0x optional, comma or space)
 *
 * A watched address that is NEVER called is reported by name at exit. That
 * is the negative this instrument must be able to print: silence from a
 * watch list is otherwise indistinguishable from "the address was wrong".
 */

#define ICALL_WATCH_MAX 16
int             g_icall_watch_n;            /* 0 = disabled; read by the macro */
static uint32_t icall_watch_va[ICALL_WATCH_MAX];
static uint64_t icall_watch_hits[ICALL_WATCH_MAX];
static int      icall_watch_depth;
static uint64_t icall_watch_printed;
static uint64_t icall_watch_cap = 200;      /* XBOX_ICALL_WATCH_MAX overrides */

static int icall_watch_index(uint32_t va)
{
    for (int i = 0; i < g_icall_watch_n; i++)
        if (icall_watch_va[i] == va) return i;
    return -1;
}

void recomp_icall_watch_init(void)
{
    const char *e = getenv("XBOX_ICALL_WATCH");
    if (!e || !e[0]) return;

    const char *cap = getenv("XBOX_ICALL_WATCH_MAX");
    if (cap && cap[0]) icall_watch_cap = strtoull(cap, NULL, 0);

    /* Parse loudly: a typo'd address must not be silently skipped, because a
       silently-dropped entry makes the watch report "never called" about an
       address it was never actually watching. */
    const char *p = e;
    int dropped = 0;
    while (*p) {
        while (*p == ',' || *p == ' ' || *p == '\t') p++;
        if (!*p) break;
        char *end = NULL;
        unsigned long long v = strtoull(p, &end, 16);
        if (end == p) {
            fprintf(stderr, "[WATCH] cannot parse \"%s\" as a hex VA --"
                            " nothing after it was parsed either\n", p);
            dropped++;
            break;
        }
        if (g_icall_watch_n >= ICALL_WATCH_MAX) {
            fprintf(stderr, "[WATCH] more than %d addresses given; 0x%08X"
                            " and everything after it is IGNORED\n",
                    ICALL_WATCH_MAX, (uint32_t)v);
            dropped++;
            break;
        }
        icall_watch_va[g_icall_watch_n++] = (uint32_t)v;
        p = end;
    }

    fprintf(stderr, "[WATCH] watching %d indirect-call target%s"
                    " (first %llu calls printed)%s:\n",
            g_icall_watch_n, g_icall_watch_n == 1 ? "" : "s",
            (unsigned long long)icall_watch_cap,
            dropped ? ", SOME ENTRIES DROPPED (see above)" : "");
    for (int i = 0; i < g_icall_watch_n; i++)
        fprintf(stderr, "  0x%08X\n", icall_watch_va[i]);
    fflush(stderr);
}

void recomp_icall_watch_pre(uint32_t va)
{
    int i = icall_watch_index(va);
    if (i < 0) return;
    icall_watch_hits[i]++;
    icall_watch_depth++;
    if (icall_watch_printed >= icall_watch_cap) return;
    icall_watch_printed++;

    /* g_esp points at the dummy return address the macro's caller pushed;
       the stdcall arguments start one slot above it. */
    if (!g_xbox_mem_offset) {
        /* Guest memory is not mapped yet, so the argument slots cannot be
           read. Say that rather than print zeros that look like arguments. */
        fprintf(stderr, "[WATCH] %*s-> 0x%08X(this=0x%08X) args=UNREADABLE"
                        " (guest memory not mapped yet) esp=0x%08X\n",
                (icall_watch_depth - 1) * 2, "", va, g_ecx, g_esp);
        fflush(stderr);
        return;
    }
    const volatile uint32_t *a =
        (const volatile uint32_t *)((uintptr_t)(g_esp + 4) + g_xbox_mem_offset);
    fprintf(stderr, "[WATCH] %*s-> 0x%08X(this=0x%08X)"
                    " args=[0x%08X, 0x%08X, 0x%08X, 0x%08X] esp=0x%08X\n",
            (icall_watch_depth - 1) * 2, "",
            va, g_ecx, a[0], a[1], a[2], a[3], g_esp);
    fflush(stderr);
}

void recomp_icall_watch_post(uint32_t va)
{
    int i = icall_watch_index(va);
    if (i < 0) return;
    if (icall_watch_printed <= icall_watch_cap) {
        fprintf(stderr, "[WATCH] %*s<- 0x%08X returned eax=0x%08X"
                        " (al=0x%02X) esp=0x%08X\n",
                (icall_watch_depth - 1) * 2, "",
                va, g_eax, g_eax & 0xFF, g_esp);
        fflush(stderr);
    }
    icall_watch_depth--;
}

void recomp_icall_watch_report(void)
{
    if (!g_icall_watch_n) return;
    fprintf(stderr, "[WATCH] call counts for the %d watched target%s:\n",
            g_icall_watch_n, g_icall_watch_n == 1 ? "" : "s");
    for (int i = 0; i < g_icall_watch_n; i++) {
        if (icall_watch_hits[i])
            fprintf(stderr, "  0x%08X  x%llu\n", icall_watch_va[i],
                    (unsigned long long)icall_watch_hits[i]);
        else
            fprintf(stderr, "  0x%08X  NEVER CALLED -- this run never reached"
                            " it INDIRECTLY. A direct call to it is not"
                            " counted here, and neither is a call made before"
                            " the watch was initialised.\n",
                    icall_watch_va[i]);
    }
    if (icall_watch_printed >= icall_watch_cap)
        fprintf(stderr, "  print cap of %llu reached; later calls were counted"
                        " but not printed (XBOX_ICALL_WATCH_MAX raises it)\n",
                (unsigned long long)icall_watch_cap);
    fflush(stderr);
}

/* Self-test: watch an address, drive both hooks by hand, and assert the
   counter moved and the never-called path reports. Proves the instrument
   fires in the shipping binary, not in a test build nobody runs. */
void recomp_icall_watch_selftest(void)
{
    int      saved_n = g_icall_watch_n;
    uint32_t saved_va[ICALL_WATCH_MAX];
    uint64_t saved_hits[ICALL_WATCH_MAX];
    memcpy(saved_va, icall_watch_va, sizeof saved_va);
    memcpy(saved_hits, icall_watch_hits, sizeof saved_hits);

    g_icall_watch_n = 2;
    icall_watch_va[0] = 0x00111111u;   /* driven below: must count */
    icall_watch_va[1] = 0x00222222u;   /* never driven: must say NEVER CALLED */
    icall_watch_hits[0] = icall_watch_hits[1] = 0;

    recomp_icall_watch_pre(0x00111111u);
    recomp_icall_watch_post(0x00111111u);
    recomp_icall_watch_pre(0x00333333u);   /* unwatched: must NOT count */

    int ok = (icall_watch_hits[0] == 1 && icall_watch_hits[1] == 0
              && icall_watch_depth == 0);
    fprintf(stderr, "[WATCH] SELFTEST %s: watched target counted %llu (want 1),"
                    " unwatched target counted 0, depth %d (want 0)."
                    " The NEVER CALLED line below is the expected negative.\n",
            ok ? "passed" : "FAILED",
            (unsigned long long)icall_watch_hits[0], icall_watch_depth);
    recomp_icall_watch_report();

    g_icall_watch_n = saved_n;
    memcpy(icall_watch_va, saved_va, sizeof saved_va);
    memcpy(icall_watch_hits, saved_hits, sizeof saved_hits);
    icall_watch_depth = 0;
    icall_watch_printed = 0;
    fflush(stderr);
}

/* ── listscan: catch the tail-shift memcpy before it walks off the heap ────
 *
 * The run ends in the CRT memcpy at 0x003D5890 reading Xbox VA 0x02902194,
 * called from the list-remove at 0x00275920+0x318, which computes
 *
 *     memcpy(base + idx*4, base + idx*4 + 4, ((count - 1) - idx) * 4)
 *
 * UNSIGNED. If idx is not strictly below the decremented count -- an empty
 * list gives count-1 = 0xFFFFFFFF -- the length is near 4 GB and the copy
 * walks off the end of the heap. This is rc-defect-listscan, whose recorded
 * next step is to LOG the real count/base/idx on a faulting trial instead of
 * inferring them.
 *
 * The observer sits on memcpy, NOT on the list-remove. --wrap only redirects
 * references that cross an object-file boundary, and the list-remove's caller
 * (sub_00289F50) sits in the same generated chunk as the list-remove itself,
 * so a wrapper there is bound at compile time and never fires -- the first
 * version of this instrument reported "0 calls" while the stack showed the
 * function running. memcpy lives in recomp_0021.c and its callers do not, so
 * this boundary is real. It only observes: it prints and delegates.
 */
extern void __real_sub_003D5890(void);

/* Anything at or above this is not a copy any of this game's structures make;
   the heap itself is 48 MB. Picked to be far below the ~4 GB underflow and
   far above a legitimate buffer copy. */
#define LS_IMPLAUSIBLE 0x01000000u

static uint64_t ls_calls, ls_bad, ls_max;

void __wrap_sub_003D5890(void)
{
    uint32_t dst  = MEM32(g_esp + 4);
    uint32_t src  = MEM32(g_esp + 8);
    uint32_t size = MEM32(g_esp + 12);

    ls_calls++;
    if (size > ls_max) ls_max = size;

    if (size >= LS_IMPLAUSIBLE) {
        ls_bad++;
        /* Every one of these, uncapped: this is the interesting case, and the
           run does not survive many of them. */
        fprintf(stderr, "[LISTSCAN] memcpy #%llu IMPLAUSIBLE LENGTH:"
                        " dst=0x%08X src=0x%08X size=0x%08X (%u)\n",
                (unsigned long long)ls_calls, dst, src, size, size);
        /* src = dst + 4 is the tail-shift signature; then dst is base+idx*4,
           and the length says how far past the end it is about to read. */
        if (src == dst + 4)
            fprintf(stderr, "  tail-shift shape (src == dst+4): this is the"
                            " list-remove at 0x00275920. size/4 = %u elements,"
                            " i.e. (count-1) - idx underflowed.\n", size / 4);
        fflush(stderr);
    }

    __real_sub_003D5890();
}

void listscan_report(void)
{
    if (ls_calls == 0) {
        fprintf(stderr, "[LISTSCAN] the memcpy wrapper at 0x003D5890 never"
                        " fired: 0 calls. That is NOT evidence of a healthy"
                        " run -- check that --wrap=sub_003D5890 is in the link"
                        " line and that its callers are in other chunks.\n");
        fflush(stderr);
        return;
    }
    fprintf(stderr, "[LISTSCAN] %llu memcpy calls, largest length 0x%llX,"
                    " %llu at or above the 0x%X implausible threshold\n",
            (unsigned long long)ls_calls, (unsigned long long)ls_max,
            (unsigned long long)ls_bad, LS_IMPLAUSIBLE);
    fflush(stderr);
}

/* ── listscan part 2: the name lookup whose miss produces the bad length ───
 *
 * sub_00275920 does not take an index; it asks for one:
 *
 *     find(this, name = item+8, &out_b, &out_idx)   -- sub_0026B390
 *     memcpy(base + idx*4, base + idx*4 + 4, ((count-1) - idx) * 4)
 *
 * and the measured length says idx == count exactly, i.e. the lookup MISSED
 * and the caller shifted anyway. The key is a STRING at item+8 (the caller
 * sub_00289F50 takes strlen of it and re-registers under it), so a miss is
 * either a name that is empty/garbage by the time we get here -- an upstream
 * data problem -- or a compare that is wrong. Printing the name and the count
 * separates those two without further inference.
 *
 * sub_0026B390 is in recomp_0013.c and has no callers in that chunk, so
 * --wrap really binds here (issue #4).
 */
extern void __real_sub_0026B390(void);

static uint64_t fnd_calls, fnd_miss;
static uint32_t fnd_last_self, fnd_last_count, fnd_last_name, fnd_last_base;
static int fnd_in_flight;

/* Copy a guest NUL-terminated string out for printing, bounded. Reports what
   it truncated rather than silently shortening. */
static void ls_guest_str(uint32_t va, char *out, size_t cap)
{
    size_t i = 0;
    if (!va) { snprintf(out, cap, "<NULL>"); return; }
    for (; i + 1 < cap; i++) {
        uint8_t c = *(volatile uint8_t *)((uintptr_t)(uint32_t)(va + i) + g_xbox_mem_offset);
        if (!c) break;
        out[i] = (c >= 0x20 && c < 0x7F) ? (char)c : '?';
    }
    out[i] = 0;
}

void __wrap_sub_0026B390(void)
{
    uint32_t self    = g_ecx;
    uint32_t name_va = MEM32(g_esp + 4);
    uint32_t flag_va = MEM32(g_esp + 8);    /* out: caller inits it to 0 */
    uint32_t idx_va  = MEM32(g_esp + 12);   /* out: index */
    uint32_t count   = self ? MEM32(self + 4) : 0;

    fnd_calls++;

    /* Record the call BEFORE making it. The interesting lookup is the one that
       does not come back -- the search faulted inside __real_ with a wild
       element pointer, and because this wrapper only logged on return, the
       fatal call was the one call that printed nothing. listfind_report runs
       from the crash handler, so this is what it prints. */
    fnd_last_self  = self;
    fnd_last_count = count;
    fnd_last_name  = name_va;
    fnd_last_base  = self ? MEM32(self + 0x10) : 0;
    fnd_in_flight  = 1;

    __real_sub_0026B390();
    fnd_in_flight = 0;

    {
        uint32_t idx  = idx_va ? MEM32(idx_va) : 0xFFFFFFFFu;
        uint32_t flag = flag_va ? MEM32(flag_va) : 0;
        char name[64];

        /* No verdict here. Twice now a verdict on this line has been wrong:
           first "MISS" for every idx == count, which is the normal
           append-here answer; then "hit" for every idx < count, which is just
           as often a sorted INSERTION POINT -- "_refCount" lands at index 0 of
           a table holding "igObject" because '_' sorts before 'i', and nothing
           was found. This is a binary search, so the index alone cannot say
           whether the key was there. Print the numbers and let the reader
           decide; idx == count is still worth flagging because that is the one
           shape the remove path turns into a ~4GB memcpy. */
        if (idx >= count) {
            fnd_miss++;
            ls_guest_str(name_va, name, sizeof name);
            fprintf(stderr, "[LISTSCAN] find #%llu idx==count: this=0x%08X"
                            " count=%u idx=%u flag=%u eax=0x%08X name=\"%s\""
                            " -- a remove here would shift"
                            " ((count-1)-idx)*4 = 0x%08X bytes\n",
                    (unsigned long long)fnd_calls, self, count, idx, flag,
                    g_eax, name, (uint32_t)(((count - 1) - idx) * 4));
            fflush(stderr);
        } else if (fnd_calls <= 5) {
            ls_guest_str(name_va, name, sizeof name);
            fprintf(stderr, "[LISTSCAN] find #%llu idx<count: count=%u idx=%u"
                            " flag=%u eax=0x%08X name=\"%s\" (insertion point"
                            " or match -- the index alone does not say which)\n",
                    (unsigned long long)fnd_calls, count, idx, flag, g_eax, name);
            fflush(stderr);
        }
    }
}

void listfind_report(void)
{
    if (fnd_in_flight) {
        char name[64];
        ls_guest_str(fnd_last_name, name, sizeof name);
        fprintf(stderr, "[LISTSCAN] the search was IN FLIGHT when this ended:"
                        " call #%llu this=0x%08X count=%u base=0x%08X"
                        " name=\"%s\" -- it did not return\n",
                (unsigned long long)fnd_calls, fnd_last_self, fnd_last_count,
                fnd_last_base, name);
    }
    if (fnd_calls == 0) {
        fprintf(stderr, "[LISTSCAN] the find wrapper at 0x0026B390 never fired:"
                        " 0 calls. Not evidence of anything -- check the link"
                        " line and issue #4 before reading this as a negative.\n");
    } else {
        fprintf(stderr, "[LISTSCAN] %llu find calls, %llu returned idx =="
                        " count (the append-at-the-end answer -- NOT an error"
                        " count; only a remove on one of these underflows)\n",
                (unsigned long long)fnd_calls, (unsigned long long)fnd_miss);
    }
    fflush(stderr);
}

/* ── listscan part 3: the isolation self-test ─────────────────────────────
 *
 * sub_00275920 is the list remove+tail-shift, and its caller sub_00289F50 is
 * one of the functions the recompiler grouped into the same chunk. Before
 * `recomp --isolate` existed, a --wrap here was bound at compile time and this
 * wrapper measurably never fired: it reported "0 calls" on a run whose crash
 * stack contained sub_00275920+0x318 (issue #4).
 *
 * So it doubles as the self-test for isolation. It is in xbox/overrides.json,
 * which makes the recompiler emit it alone in recomp_iso_00275920.c; if this
 * ever reports 0 calls again on a run that reaches the ARK symbol table, the
 * isolation has regressed and EVERY other override is suspect too -- including
 * the native heap.
 */
extern void __real_sub_00275920(void);

static uint64_t rm_calls, rm_underflow;

void __wrap_sub_00275920(void)
{
    uint32_t self  = g_ecx;                       /* __thiscall this */
    uint32_t count = self ? MEM32(self + 4) : 0;

    {
        /* Only a couple of removes happen on the boot path, so print every one
           rather than sampling: the interesting call is the last one, and a
           cap would drop exactly that. The key is the string at item+8, the
           same one the find looks up. */
        uint32_t item = MEM32(g_esp + 4);
        char name[64];
        rm_calls++;
        ls_guest_str(item ? item + 8 : 0, name, sizeof name);
        fprintf(stderr, "[LISTSCAN] remove #%llu this=0x%08X count=%u"
                        " item=0x%08X name=\"%s\"\n",
                (unsigned long long)rm_calls, self, count, item, name);
        if (count == 0) rm_underflow++;
        fflush(stderr);
    }

    __real_sub_00275920();
}

void listremove_report(void)
{
    if (rm_calls == 0) {
        fprintf(stderr, "[LISTSCAN] ISOLATION SELF-TEST FAILED: the wrapper on"
                        " sub_00275920 recorded 0 calls. Either the run died"
                        " before the ARK symbol table, or recomp --isolate did"
                        " not emit recomp_iso_00275920.c and --wrap was bound"
                        " intra-chunk again (issue #4). In the second case"
                        " EVERY override is silently absent, including the"
                        " native heap.\n");
    } else {
        fprintf(stderr, "[LISTSCAN] isolation self-test passed: %llu calls"
                        " reached the wrapper on sub_00275920 (%llu on an empty"
                        " list). --wrap is binding across the object boundary.\n",
                (unsigned long long)rm_calls, (unsigned long long)rm_underflow);
    }
    fflush(stderr);
}
