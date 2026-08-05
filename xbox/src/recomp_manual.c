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
recomp_func_t recomp_lookup_manual(uint32_t xbox_va)
{
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
