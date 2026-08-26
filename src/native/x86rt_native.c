/*
 * The shared native runtime: dispatch across every linked recompiled module.
 * See x86rt_native.h for why dispatch keys on the mapped address rather than
 * the guest entry point.
 */
#include "x86rt.h"
#include "x86_reached.h"
#include "x86rt_native.h"
#include "threads.h"
#include "guest_heap.h"
#include <time.h>
#include <errno.h>
#include <signal.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/uio.h>
#include <dlfcn.h>
#include <fcntl.h>

static X86Module *g_head;
static const X86Fn *find(X86Module *m, uint32_t addr);

/* Native overrides: (module, linked entry point) -> C implementation.
   Registered from the subsystem files by x86_register_override; the dispatcher
   checks this table BEFORE the recompiled body (x86_native_call_at). Declared
   in the .c where the override belongs, never generated.

   The key is a module NAME plus the entry point at that module's PREFERRED
   base, never a bare address. Every libIG*.dll is linked for 0x10000000, so a
   bare entry point names up to eight different functions -- 0x10002520 is a
   real function in eight of this game's modules -- while dispatch works in
   MAPPED addresses, which are unique. A table keyed on the linked address
   therefore both misses its intended target (the module was relocated) and
   answers for whichever unrelated module happened to keep the preferred base.
   That is the same collision this file's header describes for the function
   tables, and it is why registration is resolved against a named module. */
#define X2_MAX_OVERRIDES 128
static struct {
    const char     *module;      /* module name as pe_map/x86_module_register knows it */
    uint32_t        linked_ep;   /* entry point at that module's PREFERRED base */
    uint32_t        mapped_ep;   /* where it actually landed; valid once resolved */
    x86_override_fn fn;
} g_override[X2_MAX_OVERRIDES];
static int g_noverride;
static int g_overrides_resolved;

/* ---- per-function override slots ----------------------------------------
 *
 * The generated code carries one slot per function and a checked entry that
 * reads it; each chunk registers its own (linked ep -> slot address) table
 * from a constructor. Binding an override is therefore a POINTER WRITE at
 * resolve time, and the emitted C is identical whether or not anything is
 * overridden.
 *
 * That is the whole point. The emitter used to regex-scan src/native for
 * x86_register_override calls and route only the addresses it recognised, so
 * (a) every override change re-emitted the module and (b) a registration the
 * regex could not read -- a named constant instead of a hex literal -- was
 * silently not routed, leaving an override that registers, resolves, and
 * never fires.
 */
typedef struct ChunkSlots {
    const char             *module;
    const uint32_t         *base;      /* the chunk's module base variable */
    const X86OverrideSlot  *slots;
    int                     n;
    struct ChunkSlots      *next;
} ChunkSlots;

static ChunkSlots *g_chunks;
static int g_chunk_count;
static long g_slot_count;

void x86_override_slots_register(const char *module, const uint32_t *base,
                                 const X86OverrideSlot *slots, int n)
{
    ChunkSlots *c = (ChunkSlots *)calloc(1, sizeof *c);
    if (!c) {
        fprintf(stderr, "x86_override_slots_register: out of memory "
                        "registering %d slot(s) for %s\n", n, module);
        abort();
    }
    c->module = module;
    c->base = base;
    c->slots = slots;
    c->n = n;
    c->next = g_chunks;
    g_chunks = c;
    g_chunk_count++;
    g_slot_count += n;
}

/* The slot for one (module, linked ep), or NULL. Linear over chunks and
   binary-searchable within one would be faster, but this runs once per
   override at startup -- tens of lookups, not millions. */
static x86_override_fn *override_slot_for(const char *module, uint32_t ep)
{
    ChunkSlots *c;
    for (c = g_chunks; c; c = c->next) {
        int i;
        if (!c->module || strcmp(c->module, module)) continue;
        for (i = 0; i < c->n; i++)
            if (c->slots[i].linked_ep == ep) return c->slots[i].slot;
    }
    return NULL;
}

/* Denominators, so "the override did not fire" can be told from "no generated
   code registered any slots at all" -- which is what a build that linked the
   runtime without the generated chunks looks like. */
long x86_override_slot_count(void) { return g_slot_count; }
int  x86_override_chunk_count(void) { return g_chunk_count; }

void x86_register_override(const char *module, uint32_t linked_ep,
                           x86_override_fn fn)
{
    int i;
    if (!module || !*module) {
        fprintf(stderr, "x86_register_override: 0x%08x registered with no "
                        "module name. An override is only meaningful against "
                        "the module that owns the entry point.\n", linked_ep);
        abort();
    }
    for (i = 0; i < g_noverride; i++) {
        if (g_override[i].linked_ep == linked_ep
                && !strcmp(g_override[i].module, module)) {
            fprintf(stderr, "x86_register_override: %s 0x%08x registered "
                            "TWICE; the new function replaces the old. An "
                            "override declared in two files is a defect -- "
                            "naming it here.\n", module, linked_ep);
            g_override[i].fn = fn;
            return;
        }
    }
    if (g_noverride >= X2_MAX_OVERRIDES) {
        fprintf(stderr, "x86_register_override: the table holds %d and is "
                        "full; %s 0x%08x is NOT registered. Raise "
                        "X2_MAX_OVERRIDES rather than letting an override "
                        "silently not fire.\n",
                X2_MAX_OVERRIDES, module, linked_ep);
        abort();
    }
    g_override[g_noverride].module    = module;
    g_override[g_noverride].linked_ep = linked_ep;
    g_override[g_noverride].mapped_ep = 0;
    g_override[g_noverride].fn        = fn;
    g_noverride++;
}

int x86_override_count(void) { return g_noverride; }

/* Resolve ONE (module, linked ep) to the mapped address dispatch will compare.
   Returns 0 and fills *mapped_out on success; non-zero with a reason in `why`
   when the pair could not be resolved. Split out from the loop below so the
   rejection paths can be exercised by --override-selftest: a resolver that
   aborts on every failure cannot be shown to accept the right things and
   reject the wrong ones without a way to ask it. */
int x86_override_resolve_check(const char *module, uint32_t linked_ep,
                               uint32_t *mapped_out, char *why, size_t whyn)
{
    X86Module *m;
    uint32_t mapped;
    for (m = g_head; m; m = m->next)
        if (!strcmp(m->name, module)) break;
    if (!m) {
        snprintf(why, whyn, "module %s is NOT mapped -- either the name is "
                            "wrong or it was not linked into this build",
                 module);
        return 1;
    }
    if (linked_ep < m->preferred || linked_ep >= m->preferred + m->size) {
        snprintf(why, whyn, "0x%08x is outside %s's image (0x%08x + 0x%x)",
                 linked_ep, module, m->preferred, m->size);
        return 1;
    }
    mapped = *m->base + (linked_ep - m->preferred);
    if (!find(m, mapped)) {
        snprintf(why, whyn, "0x%08x is not the entry point of any recompiled "
                            "body in %s -- an override on a mid-function "
                            "address is never dispatched to",
                 linked_ep, module);
        return 1;
    }
    *mapped_out = mapped;
    return 0;
}

/* Turn every (module, linked ep) into the mapped address dispatch will see.
   Called once, after every module has registered and been mapped: the
   registrations run from constructors, which is before pe_map has placed
   anything, so the mapped address cannot be known at registration time.

   Every failure here is fatal by design. An override that does not resolve is
   invisible at runtime -- the game runs, the native code simply never executes
   and the recompiled body answers instead -- which is indistinguishable from a
   working build until something downstream is wrong for reasons that look
   unrelated. */
void x86_overrides_resolve(void)
{
    int i, bad = 0;
    for (i = 0; i < g_noverride; i++) {
        char why[256];
        uint32_t mapped = 0;
        x86_override_fn *slot;
        if (x86_override_resolve_check(g_override[i].module,
                                       g_override[i].linked_ep,
                                       &mapped, why, sizeof why) != 0) {
            fprintf(stderr, "x86_overrides_resolve: override for %s 0x%08x "
                            "could not be resolved: %s\n",
                    g_override[i].module, g_override[i].linked_ep, why);
            bad++;
            continue;
        }
        g_override[i].mapped_ep = mapped;
        slot = override_slot_for(g_override[i].module,
                                 g_override[i].linked_ep);
        if (!slot) {
            fprintf(stderr, "x86_overrides_resolve: %s 0x%08x resolved to a "
                            "mapped body but the generated code registered no "
                            "override slot for it. %d chunk(s) and %ld slot(s) "
                            "are registered -- if that is 0, no generated "
                            "chunk ran its constructor.\n",
                    g_override[i].module, g_override[i].linked_ep,
                    g_chunk_count, g_slot_count);
            bad++;
            continue;
        }
        *slot = g_override[i].fn;
    }
    if (bad) {
        fprintf(stderr, "x86_overrides_resolve: %d of %d override(s) could not "
                        "be resolved. Refusing to run: a silently absent "
                        "override looks exactly like a working build.\n",
                bad, g_noverride);
        abort();
    }
    g_overrides_resolved = 1;
    printf("overrides: %d native override(s) bound into their function's own "
           "slot, out of %ld slot(s) in %d generated chunk(s)\n",
           g_noverride, g_slot_count, g_chunk_count);
}

static int thunk_call(uint32_t addr, CPU *C);
static FILE *g_sc_out;
static int   g_sc_armed;
static unsigned long g_sc_records;
extern volatile sig_atomic_t x2_report_now;   /* heartbeat: set when the run stops */

/* The current guest body, written on every dispatch (guest body or import
   stub), read by the X2_PROFILE sampler thread. Declared here, at the top,
   because the two dispatch paths that write it precede the profiler block. */
volatile uint32_t g_sample_ep;

/* X2_GUEST_WATCH diagnostic: which body ran right after a guest address went
   to zero. Set once from the environment; see the dispatch path. */
uint32_t g_guest_watch_addr;
static uint32_t g_last_dispatch_ep;

/* X2_WRITE_WATCH=<guest-addr>[:<value>]: armed by x86_write_watch_arm;
   WR8/16/32 call x2_write_watch_fire the moment the watched guest address is
   written.

   It does NOT stop at the first hit. A stack address is reused by every frame
   that passes through it, so on a guest stack the first write is almost never
   the interesting one: watching a /GS cookie slot, the single shot was spent
   on an unrelated frame's write hundreds of frames before the overrun, and the
   watch then sat disarmed through the corruption it was armed for and reported
   nothing. A one-shot watch on a hot address reports the wrong writer and
   looks like an answer.

   So every write is reported, and the optional :<value> filter is what narrows
   a hot slot to the interesting case ("who writes ZERO here") instead of
   narrowing it to "whoever got here first". */
volatile uint32_t x2_write_watch_addr;
static int           g_ww_filter;        /* a :<value> filter was given */
static uint32_t      g_ww_value;         /* ... and the value it selects */
static unsigned long g_ww_hits;          /* writes seen, filter included */
static unsigned long g_ww_reported;

/* Cap the BORING case only: the first few writes, plus EVERY write of the
   filtered value. A cap that hides the interesting write is how a watch
   reports nothing and reads as "nothing happened". */
#define WW_REPORT_FIRST 8

void x2_write_watch_fire(uint32_t a, uint32_t v)
{
    extern const char *x86_native_name_at(uint32_t);
    const char *nm;
    X86Module *m;

    g_ww_hits++;
    if (g_ww_filter && v != g_ww_value) return;
    /* Unfiltered: the first few, plus the two writes that MATTER on a /GS
       cookie slot -- the store of the process cookie (the frame arming its
       tripwire) and any store of zero (the tripwire being wiped). Reporting
       only zeros shows the wipes but not which frame's cookie was wiped, so
       the pair is what makes the sequence readable. */
    if (!g_ww_filter && g_ww_reported >= WW_REPORT_FIRST
            && v != 0 && v != RD32(0x006f38f8)) return;

    nm = x86_native_name_at(g_sample_ep);
    m  = x86_module_for(g_sample_ep);
    g_ww_reported++;
    /* g_sample_ep is the last DISPATCHED body, which is the writer only when
       the writer was reached through the dispatcher. Reached by a direct C
       call it names an ANCESTOR -- narrowing, not naming, and it says so. */
    fprintf(stderr, "[WWATCH] write #%lu to 0x%08x = 0x%08x (process cookie "
                    "0x%08x); last dispatched body 0x%08x %s%s%s%s\n",
            g_ww_hits, a, v, RD32(0x006f38f8), g_sample_ep,
            nm ? "" : "in ", nm ? nm : (m ? m->name : "???"),
            (nm || !m) ? "" : " +offset",
            v == 0 ? "   <-- ZERO"
                   : (v == RD32(0x006f38f8) ? "   <-- /GS cookie stored" : ""));
}

/* How many writes the watch saw, so a run can report a real denominator --
   "0 of 0" and "0 of 12,043" are different answers. */
unsigned long x86_write_watch_hits(void) { return g_ww_hits; }

void x86_write_watch_arm(const char *arg)
{
    const char *colon;
    if (!arg || !*arg) return;
    x2_write_watch_addr = (uint32_t)strtoul(arg, NULL, 0);
    colon = strchr(arg, ':');
    if (colon) {
        g_ww_filter = 1;
        g_ww_value = (uint32_t)strtoul(colon + 1, NULL, 0);
    }
    if (!x2_write_watch_addr) {
        fprintf(stderr, "X2_WRITE_WATCH=%s parsed to address 0; the watch is "
                        "NOT armed and nothing will be reported.\n", arg);
        return;
    }
    if (g_ww_filter)
        fprintf(stderr, "X2_WRITE_WATCH=0x%08x:0x%08x: every guest write of "
                        "that value to that address is reported, all of them.\n",
                x2_write_watch_addr, g_ww_value);
    else
        fprintf(stderr, "X2_WRITE_WATCH=0x%08x: the first %d guest write(s) to "
                        "this address are reported, plus every /GS cookie store "
                        "and every write of ZERO.\n",
                x2_write_watch_addr, WW_REPORT_FIRST);
}
static void ring_note(const char *what, uint32_t addr, uint32_t base,
                      uint32_t in, uint32_t out, uint32_t ret);
static void hotep_count(uint32_t ep, unsigned long long ns);
static unsigned long g_return_to_calls;
extern const CPU *g_cpu_current;

/* Guard for the wall-time attribution probe (see below). Zero in the default
   build, which is one predictable branch per dispatch; set by X2_HOTEP. */
static int g_probe_time;

/* Cumulative EXCLUSIVE ns inside host import stubs and inside guest bodies
   since arming, for x86_probe_time_delta. Read with the same torn-read trade
   as the crossing counter.
 *
   "Exclusive" matters: a dispatched body runs nested dispatches and imports
   inside its own span, so an inclusive span counter would charge the same
   wall time at every nesting level (a 5s interval once measured 167s of
   "guest" time on a single-threaded scheduler -- impossible). The span stack
   below charges each level only its own compute: a level's span minus the
   spans of every child level pushed on top of it. Direct guest-to-guest
   calls never dispatch, so they remain inside the enclosing body's exclusive
   span, which is exactly the attribution wanted for naming a hot body. */
static unsigned long long g_host_import_ns, g_guest_body_ns;

static inline unsigned long long probe_ns_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long long)ts.tv_sec * 1000000000ull + ts.tv_nsec;
}

#define SPAN_STACK_MAX 64
static unsigned long long g_span_start[SPAN_STACK_MAX];
static unsigned long long g_span_child[SPAN_STACK_MAX];
static int g_span_depth;

static inline void span_push(void)
{
    if (g_span_depth < SPAN_STACK_MAX) {
        g_span_start[g_span_depth] = probe_ns_now();
        g_span_child[g_span_depth] = 0;
    }
    g_span_depth++;
}

/* Returns this level's exclusive ns and charges its full span to the parent
   level so the parent's own exclusive time excludes it. */
static inline unsigned long long span_pop(void)
{
    unsigned long long full, excl;
    if (g_span_depth <= 0) return 0;
    g_span_depth--;
    if (g_span_depth >= SPAN_STACK_MAX) return 0; /* slot overflowed, lost */
    full = probe_ns_now() - g_span_start[g_span_depth];
    excl = full - g_span_child[g_span_depth];
    if (g_span_depth > 0 && g_span_depth - 1 < SPAN_STACK_MAX)
        g_span_child[g_span_depth - 1] += full;
    return excl;
}

/* Not used by the shared runtime itself, but the emitted bodies of a
   single-module build still reference the plain symbol. */
uint32_t g_imgbase = 0x10000000U;
uint32_t g_image_lo, g_image_hi;
int x86_allow_fallback;

void x86_module_register(X86Module *m)
{
    m->next = g_head;
    g_head = m;
}

X86Module *x86_modules(void) { return g_head; }

X86Module *x86_module_for(uint32_t addr)
{
    X86Module *m;
    for (m = g_head; m; m = m->next) {
        uint32_t b = *m->base;
        /* size 0 means the host never mapped this module. Saying so beats
           returning NULL, which reads as "that address is host memory". */
        if (b && !m->size) {
            fprintf(stderr, "x86_module_for: %s has a base but no size -- the "
                            "host mapped it and did not record how big it is, "
                            "so every lookup into it will miss\n", m->name);
            abort();
        }
        if (b && addr >= b && addr - b < m->size) return m;
    }
    return NULL;
}

/* Binary search over the module's function table, which recomp.py native
   emits SORTED by entry point. It used to be linear ("5769 entries is small
   enough"), and that was true until the load window was measured: the arena
   pool's 2-instruction isActive is reached only by DISPATCH, the load calls
   it hundreds of thousands of times per frame, and a linear scan of a
   ~6000-entry table on every dispatch made it the single most-sampled body
   in the load (15.6%) with the check itself doing no work. log2(6000) is 13
   compares. A duplicate EP cannot occur: interior entries are addresses
   inside another body, never a function start. */
static const X86Fn *find(X86Module *m, uint32_t addr)
{
    uint32_t ep = m->preferred + (addr - *m->base);
    int lo = 0, hi = m->nfns - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        uint32_t mep = m->fns[mid].ep;
        if (mep == ep) return &m->fns[mid];
        if (mep < ep) lo = mid + 1;
        else hi = mid - 1;
    }
    return NULL;
}

/*
 * One-shot triggers: run host code the first time the guest calls a given
 * address.
 *
 * Substituting an engine class cannot happen at module-init time -- ARK
 * registration goes through igGetMemoryPool, and the pools do not exist until
 * the exe's engine startup has run, so registering early faults on a NULL pool
 * inside libIGCore. Nor can it happen after --run returns, by which point the
 * engine has torn down. It has to happen at a MOMENT DURING the run, and the
 * only reliable way to name that moment is an address the engine itself
 * reaches once it is ready.
 *
 * Deliberately fires BEFORE the body, and deliberately once: a trigger that
 * refires would re-register a class on every call.
 */
#define MAX_TRIG 8
static struct { uint32_t addr; int (*fn)(void); const char *why;
                int fired, active; } g_trig[MAX_TRIG];
static int g_ntrig;

void x86_at_first_call(uint32_t addr, int (*fn)(void), const char *why)
{
    if (g_ntrig == MAX_TRIG) {
        fprintf(stderr, "x86_at_first_call: no room for a trigger on 0x%08x\n",
                addr);
        abort();
    }
    g_trig[g_ntrig].addr = addr;
    g_trig[g_ntrig].fn = fn;
    g_trig[g_ntrig].why = why;
    g_trig[g_ntrig].fired = 0;
    g_ntrig++;
}

/* Whether every armed trigger actually fired. A trigger that never fires is
   the failure mode that looks like success: the run completes, nothing was
   substituted, and nothing said so. */
int x86_triggers_report(void)
{
    int i, unfired = 0;
    for (i = 0; i < g_ntrig; i++)
        if (!g_trig[i].fired) {
            fprintf(stderr, "trigger NEVER FIRED: 0x%08x (%s) -- the guest "
                            "never called that address, so the host code "
                            "waiting on it did not run.\n",
                    g_trig[i].addr, g_trig[i].why);
            unfired++;
        }
    return unfired;
}

/*
 * X2_EPCOUNT=0x004a11c0,0x004a1320 -- how often a body is ENTERED, in an
 * ordinary build.
 *
 * X2_ARGS answers this too but only in a trace build (I047), and a trace build
 * is slow enough that the run never reaches the scene the question is about.
 * This costs one comparison per DISPATCHED call, which is the only way a body
 * with no direct call site can be reached at all -- and the two script-launch
 * functions in XMen2.exe have exactly zero direct call sites, so every entry
 * they get comes through here.
 *
 * It prints its counts AT ZERO and with the run's dispatch total, because
 * "this function was never entered" and "the counter never ran" are the two
 * answers that must not look alike -- which is the whole reason the previous
 * attempt at this question was worthless.
 */
static int args_string_at(uint32_t a, char *out, size_t cap);

#define EPCOUNT_MAX 8
#define EPCOUNT_WORDS 64
static struct {
    uint32_t ep;
    unsigned long n;
    /* The RAW argument words, distinct, first-seen order -- and nothing is
     * dereferenced here. The first attempt at this decoded them as strings ON
     * THE DISPATCH PATH and the run died at 12 s with the guest executing a
     * string (I048); reading the words is passive, and turning them into names
     * can wait until the report, when nothing is mid-call. */
    int nwords, lost;
    uint32_t word[EPCOUNT_WORDS];
    /* Distinct RETURN ADDRESSES: which code calls this. The word at ESP on
     * entry is the return address every emitted call site pushes, so reading
     * it is as passive as reading an argument -- and it is what turns "35
     * launches" into "launched from here". */
    int nrets, retlost;
    uint32_t ret[EPCOUNT_WORDS];
} g_epc[EPCOUNT_MAX];
static int g_epc_n = -1;
static unsigned long g_epc_dispatches;

static void epcount_init(void)
{
    const char *e = getenv("X2_EPCOUNT");
    char buf[256], *p, *save;
    g_epc_n = 0;
    if (!e || !*e) return;
    snprintf(buf, sizeof buf, "%s", e);
    for (p = strtok_r(buf, ",", &save); p && g_epc_n < EPCOUNT_MAX;
         p = strtok_r(NULL, ",", &save))
        g_epc[g_epc_n++].ep = (uint32_t)strtoul(p, NULL, 0);
    fprintf(stderr, "[EPC] counting entries to %d entry point(s) at the "
                    "dispatcher. A body with a DIRECT caller is a plain C call "
                    "and is invisible here; these are counted only when "
                    "dispatched.\n", g_epc_n);
}

void x86_epcount_report(void)
{
    int i;
    if (g_epc_n < 0) epcount_init();
    if (!g_epc_n) return;
    fprintf(stderr, "[EPC] %lu dispatched call(s) in this run:\n",
            g_epc_dispatches);
    for (i = 0; i < g_epc_n; i++) {
        int k, shown = 0;
        fprintf(stderr, "[EPC]   0x%08x  %lu entr%s\n", g_epc[i].ep,
                g_epc[i].n, g_epc[i].n == 1 ? "y" : "ies");
        if (!g_epc[i].n) continue;
        /* Decoded HERE, with the guest stopped, not on the dispatch path. */
        for (k = 0; k < g_epc[i].nwords; k++) {
            char buf[64];
            if (!args_string_at(g_epc[i].word[k], buf, sizeof buf)) continue;
            fprintf(stderr, "[EPC]       0x%08x -> \"%s\"\n",
                    g_epc[i].word[k], buf);
            shown++;
        }
        fprintf(stderr, "[EPC]       %d of %d distinct argument word(s) "
                        "decoded as text%s\n", shown, g_epc[i].nwords,
                g_epc[i].lost ? " (and some were dropped: the table is full)"
                              : "");
        for (k = 0; k < g_epc[i].nrets; k++) {
            const char *nm = x86_native_name_at(g_epc[i].ret[k]);
            X86Module *rm = x86_module_for(g_epc[i].ret[k]);
            fprintf(stderr, "[EPC]       called from 0x%08x%s%s%s\n",
                    g_epc[i].ret[k],
                    nm ? " -- " : (rm ? " -- in " : ""),
                    nm ? nm : (rm ? rm->name : ""),
                    (!nm && rm) ? ", not at a named body" : "");
        }
        if (g_epc[i].retlost)
            fprintf(stderr, "[EPC]       ... and %d more distinct call site(s) "
                            "past the table.\n", g_epc[i].retlost);
    }
}

/*
 * X2_STACKCHECK=<file>: record the ESP DELTA of every dispatched call while
 * armed, for tools/stackcheck.py to check against what each guest function's
 * own RET says it pops.
 *
 * A guest call must return with esp raised by 4 (the return address) plus the
 * callee's RET immediate. Nothing in this runtime has ever checked that: the
 * ring records esp_in and esp_out and prints the delta, but a delta is only
 * wrong relative to an expectation, and the expectation lives in the guest
 * binary, not here. So the runtime records and the checker -- which can read
 * the module JSON -- decides.
 *
 * The failure this exists for: FUN_0046b750 stores its /GS cookie at
 * entry_esp-4 and its epilogue reads [ESP+0x20], and those were FOUR BYTES
 * APART, so the epilogue compared a slot that was never the cookie and the
 * /GS check reported a buffer overrun that had not happened. One dword of
 * drift inside the call tree presents as memory corruption somewhere else.
 */
void x86_stackcheck_arm(int on)
{
    if (on && !g_sc_out) {
        const char *path = getenv("X2_STACKCHECK");
        if (!path || !*path) return;      /* not asked for */
        g_sc_out = fopen(path, "w");
        if (!g_sc_out) {
            fprintf(stderr, "X2_STACKCHECK=%s could not be opened for "
                            "writing; NOTHING will be recorded.\n", path);
            return;
        }
        fprintf(stderr, "X2_STACKCHECK=%s: recording the esp delta of every "
                        "dispatched call while armed.\n", path);
    }
    g_sc_armed = on;
    if (!on && g_sc_out) {
        fflush(g_sc_out);
        fprintf(stderr, "X2_STACKCHECK: %lu dispatched call(s) recorded. A "
                        "count of 0 means the armed window contained no "
                        "dispatched call, NOT that every delta was right.\n",
                g_sc_records);
    }
}

/* Records the LINKED ep and the module name, not the mapped address. Keyed on
   the mapped address, every relocated DLL missed the checker's expectation
   table and 86%% of a run came back "no known RET" -- unchecked calls counted
   as clean. The module knows where it was placed, so it converts here. */
static void stackcheck_note(X86Module *m, uint32_t ep, uint32_t in,
                            uint32_t out)
{
    if (!g_sc_armed || !g_sc_out) return;
    g_sc_records++;
    fprintf(g_sc_out, "%s %08x %08x %08x\n", m->name,
            m->preferred + (ep - *m->base), in, out);
}

#ifdef X2_DCHECK
/*
 * The direct-call stack check's report. Built only when X2_DCHECK is defined.
 *
 * It names the CALL SITE, not the callee, because the site is what a
 * disassembly listing can be opened at -- and the first site to report is the
 * one to read. Reports every violation up to a cap and then says how many more
 * it saw, so a flood is visibly a flood rather than a truncated list that
 * looks like the whole answer.
 */
static unsigned long g_dchk_calls, g_dchk_bad, g_dchk_unchecked;

/*
 * The cap is PER CALL SITE, not per run. One site inside msdia80's unwinder
 * repeated its violation thousands of times and ate a global cap of 20 before
 * any other module could report once -- the report then read as "only msdia80
 * is out of balance", which was the cap talking, not the game. Capping the
 * boring case means capping each site, so a site not seen before is always
 * heard.
 */
#define DCHK_SITES      512
#define DCHK_PER_SITE   3
static struct { uint32_t site; unsigned long n; } g_dchk_site[DCHK_SITES];
static int g_dchk_nsites, g_dchk_sites_lost;

static int dchk_should_report(uint32_t site)
{
    int i;
    for (i = 0; i < g_dchk_nsites; i++)
        if (g_dchk_site[i].site == site)
            return ++g_dchk_site[i].n <= DCHK_PER_SITE;
    if (g_dchk_nsites == DCHK_SITES) { g_dchk_sites_lost++; return 0; }
    g_dchk_site[g_dchk_nsites].site = site;
    g_dchk_site[g_dchk_nsites].n = 1;
    g_dchk_nsites++;
    return 1;
}

/* X2_DCHECK_RANGE=<lo>-<hi>: log EVERY direct call whose site is in that
   guest range, balanced or not, with the esp on both sides. Finding a drift
   that no single call causes means watching esp walk through one function and
   seeing which step it is wrong at -- a list of violations cannot show that,
   because there is no violation to list. */
static uint32_t g_dchk_lo, g_dchk_hi;
static int g_dchk_range_read;

void x86_dcall_check(uint32_t site, uint32_t esp_before, uint32_t esp_after,
                     int imm)
{
    uint32_t want;
    g_dchk_calls++;
    if (!g_dchk_range_read) {
        const char *e = getenv("X2_DCHECK_RANGE");
        g_dchk_range_read = 1;
        if (e && *e) {
            char *dash = NULL;
            g_dchk_lo = (uint32_t)strtoul(e, &dash, 0);
            if (dash && *dash == '-') g_dchk_hi = (uint32_t)strtoul(dash + 1,
                                                                   NULL, 0);
            fprintf(stderr, "[DCHK] logging every direct call in "
                            "0x%08x-0x%08x, balanced or not.\n",
                    g_dchk_lo, g_dchk_hi);
        }
    }
    if (g_dchk_hi && site >= g_dchk_lo && site < g_dchk_hi)
        fprintf(stderr, "[DSITE] 0x%08x esp %08x -> %08x (%+d, callee pops "
                        "%d)\n", site, esp_before, esp_after,
                (int)(esp_after - esp_before), imm);
    if (imm < 0) { g_dchk_unchecked++; return; }
    want = esp_before + (uint32_t)imm;
    if (esp_after == want) return;
    g_dchk_bad++;
    if (dchk_should_report(site)) {
        const char *nm = x86_native_name_at(site);
        X86Module *m = x86_module_for(site);
        fprintf(stderr, "[DCHK] call site 0x%08x%s%s returned esp 0x%08x, "
                        "expected 0x%08x (%+d) -- the callee's RET pops %d\n",
                site, nm ? " in " : (m ? " in " : " "),
                nm ? nm : (m ? m->name : "???"),
                esp_after, want, (int)(esp_after - want), imm);
    }
}

void x86_dcall_report(void)
{
    fprintf(stderr, "[DCHK] %lu direct call(s) checked, %lu out of balance, "
                    "%lu not checkable (callee ends in a tail call or its RETs "
                    "disagree). A zero here is a measurement only because the "
                    "denominator is beside it.\n",
            g_dchk_calls - g_dchk_unchecked, g_dchk_bad, g_dchk_unchecked);
    fprintf(stderr, "[DCHK] %d distinct offending call site(s)%s; each was "
                    "reported at most %d time(s).\n", g_dchk_nsites,
            g_dchk_sites_lost ? " (and the site table filled -- some are "
                                "counted but unnamed)" : "",
            DCHK_PER_SITE);
}
#endif

int x86_native_call_at(uint32_t addr, CPU *C)
{
    X86Module *m;
    if (g_ntrig) {
        int i;
        for (i = 0; i < g_ntrig; i++)
            if (!g_trig[i].fired && !g_trig[i].active
                    && g_trig[i].addr == addr) {
                /*
                 * RETRIED, not one-shot. "The engine is ready" is a state, not
                 * a call site: arming on the first createInstance fired before
                 * libIGGfx had registered igVisualContext, so the substitution
                 * found a NULL meta and correctly declined. The handler decides
                 * when it is ready and returns non-zero to disarm.
                 *
                 * `active` guards re-entry -- the handler calls guest code that
                 * itself reaches createInstance.
                 */
                g_trig[i].active = 1;
                if (g_trig[i].fn()) g_trig[i].fired = 1;
                g_trig[i].active = 0;
            }
    }
    if (thunk_call(addr, C)) return 1;
    /* A native override shadows the recompiled body. Checked BEFORE the module
       lookup so the override path skips the find() scan and the epcount/ring
       machinery -- the frame-cap override runs every frame, and routing it
       through the full dispatch bookkeeping would be the cost of a diagnostic
       on a hot path. */
    {
        int i;
        if (g_noverride && !g_overrides_resolved) {
            fprintf(stderr, "x86_native_call_at: guest code is running before "
                            "x86_overrides_resolve(); %d override(s) would be "
                            "silently skipped.\n", g_noverride);
            abort();
        }
        for (i = 0; i < g_noverride; i++)
            if (g_override[i].mapped_ep == addr) {
                uint32_t in = C->esp;
                g_override[i].fn(C);
                /* Overrides are checked TOO. A hand-written override has to
                   emulate the guest RET itself -- pop the return address and
                   whatever the callee pops -- and getting that wrong shifts
                   the guest stack by a word, which surfaces later as memory
                   corruption somewhere unrelated. Returning before this point
                   made the 19 overrides the one thing the stack check could
                   not see, which is the wrong place to have a blind spot. */
                {
                    X86Module *om = x86_module_for(addr);
                    if (om) stackcheck_note(om, addr, in, C->esp);
                }
                return 1;
            }
    }
    m = x86_module_for(addr);
    const X86Fn *f = m ? find(m, addr) : NULL;
    if (!f) return 0;
    {
        uint32_t in = C->esp;
        /* X2_GUEST_WATCH=<addr>: catch WHO zeroed a guest address. Every
           dispatch, if the watched dword has become 0, the PREVIOUS body was
           the writer (a cookie slot going to 0 is the signature of a strncpy
           zero-pad overrun). One shot. */
        if (g_guest_watch_addr && RD32(g_guest_watch_addr) == 0) {
            const char *wn = x86_native_name_at(g_last_dispatch_ep);
            fprintf(stderr, "[GWATCH] guest 0x%08x went ZERO; last body was "
                            "0x%08x %s\n",
                    g_guest_watch_addr, g_last_dispatch_ep,
                    wn ? wn : "(?)");
            g_guest_watch_addr = 0;
        }
        g_last_dispatch_ep = addr;
        /*
         * The preemption point. This is the boundary every dispatched call
         * crosses, so it is the one place a quantum can be counted without a
         * hook in every generated body -- and a guest thread that never
         * dispatches is not running guest code at all.
         */
        g_cpu_current = C;
        if (g_epc_n < 0) epcount_init();
        g_epc_dispatches++;
        g_sample_ep = addr;
        if (g_probe_time) span_push();
        if (g_epc_n) {
            int i;
            for (i = 0; i < g_epc_n; i++)
                if (g_epc[i].ep == addr) {
                    int k, w;
                    uint32_t ra = RD32(C->esp);
                    g_epc[i].n++;
                    for (k = 0; k < g_epc[i].nrets; k++)
                        if (g_epc[i].ret[k] == ra) break;
                    if (k == g_epc[i].nrets) {
                        if (g_epc[i].nrets < EPCOUNT_WORDS)
                            g_epc[i].ret[g_epc[i].nrets++] = ra;
                        else
                            g_epc[i].retlost++;
                    }
                    /* ECX (a __thiscall `this`) and the first three stack words
                       above the return address. Values only. */
                    for (w = 0; w < 4; w++) {
                        uint32_t v = w == 0
                            ? C->ecx
                            : RD32(C->esp + 4u + (uint32_t)(w - 1) * 4u);
                        for (k = 0; k < g_epc[i].nwords; k++)
                            if (g_epc[i].word[k] == v) break;
                        if (k < g_epc[i].nwords) continue;
                        if (g_epc[i].nwords < EPCOUNT_WORDS)
                            g_epc[i].word[g_epc[i].nwords++] = v;
                        else
                            g_epc[i].lost++;
                    }
                    break;
                }
        }
        f->fn(C);
        if (g_probe_time) {
            unsigned long long excl = span_pop();
            g_guest_body_ns += excl;
            hotep_count(addr, excl);
        }
        stackcheck_note(m, addr, in, C->esp);
        ring_note("guest", addr, 0, in, C->esp, 0);
    }
    return 1;
}

/*
 * The CPU state at a fault.
 *
 * A guest-to-guest call is a direct C call passing the SAME CPU pointer down,
 * so the pointer recorded at the last boundary crossing is still the live
 * register file however deep the guest has gone since. Without it a fault
 * report can name the instruction and not one operand -- which is how
 * "MOV EDI,[EAX] faulted at 0" and "but EAX was dereferenced fine two
 * instructions earlier" sat as a contradiction with no way to settle it.
 */
const CPU *g_cpu_current;

void x86_regs_dump(void)
{
    const CPU *C = g_cpu_current;
    if (!C) {
        fprintf(stderr, "[REGS] no CPU has crossed the host boundary yet, so "
                        "there is no register file to show.\n");
        return;
    }
    fprintf(stderr,
            "[REGS] eax %08x  ecx %08x  edx %08x  ebx %08x\n"
            "[REGS] esp %08x  ebp %08x  esi %08x  edi %08x\n",
            C->eax, C->ecx, C->edx, C->ebx, C->esp, C->ebp, C->esi, C->edi);
    fprintf(stderr, "[REGS] (the register file of the last body to cross the "
                    "boundary; guest-to-guest calls share it, so these are "
                    "live -- but a body that saved a register to its own C "
                    "locals is not reflected here)\n");
}

/*
 * The entry point of the function CONTAINING an address -- the greatest entry
 * point at or below it, within the same module.
 *
 * Exists so that host code can identify its own caller by ROUTINE rather than
 * by a hardcoded address. The DirectInput layer uses it to find the game's
 * gamepad re-enumeration routine: it is called from inside that routine, and
 * asking "which function am I in" is self-identifying in a way that a constant
 * in this repository would not be.
 *
 * It is an APPROXIMATION and says so: the table carries entry points, not
 * sizes, so an address in a gap Ghidra never claimed will be attributed to the
 * function before it. Callers must sanity-check what comes back -- the one
 * here checks the name -- rather than trusting the answer blind.
 */
uint32_t x86_native_entry_containing(uint32_t addr, const char **name_out)
{
    X86Module *m = x86_module_for(addr);
    uint32_t want, best = 0;
    const char *bestnm = NULL;
    int i;
    if (name_out) *name_out = NULL;
    if (!m) return 0;
    want = m->preferred + (addr - *m->base);
    for (i = 0; i < m->nfns; i++)
        if (m->fns[i].ep <= want && m->fns[i].ep > best) {
            best = m->fns[i].ep;
            bestnm = m->fns[i].name;
        }
    if (!best) return 0;
    if (name_out) *name_out = bestnm;
    return *m->base + (best - m->preferred);        /* MAPPED address */
}

const char *x86_native_name_at(uint32_t addr)
{
    X86Module *m = x86_module_for(addr);
    const X86Fn *f = m ? find(m, addr) : NULL;
    return f ? f->name : NULL;
}

/* ---- native import thunks ---------------------------------------------
 *
 * Some imports are implemented natively but are not another recompiled module,
 * so binding cannot point their IAT slot at a guest body. Most of the time
 * that is harmless -- the emitted code calls the NAMED stub and never reads
 * the slot -- but the exe's CRT startup takes GetModuleHandleA's address from
 * the IAT and calls through it, which reaches no stub at all.
 *
 * So each such slot gets a synthetic address in a range this dispatcher owns,
 * and a call to one runs the stub. The range is deliberately NOT mapped: it is
 * never executed as code and never dereferenced, so leaving it unmapped means
 * a stray READ of one faults instead of returning a plausible word.
 *
 * The test for "implemented natively" is the one thing here that must not be
 * assumed: it compares the stub's address against the aborting default. A
 * module whose import is still the weak stub gets no thunk, and stays
 * poisoned, so nothing is silently promoted to "working".
 */
#define THUNK_BASE 0x000C0000u
#define THUNK_MAX  2048

static struct { void (*stub)(CPU *); const char *mod, *sym; void *ctx; }
    g_thunk[THUNK_MAX];
static int g_nthunk;

/*
 * RAW per-thunk call counts, immune to the boundary ring's repeat-collapse.
 *
 * The ring collapses consecutive identical crossings into one entry (with a
 * count), which is right for showing history and WRONG for measuring: a hot
 * import called in a loop reads as a handful of ring entries, so "how many
 * host calls does a build frame make and to which import" needs a counter
 * that increments on EVERY call. g_ring_n under-counts exactly the tight
 * loops a load hotspot is. This grows one 8-byte word per thunk, no smoothing;
 * the read side is the per-interval probe in x86_thunk_crossings_sorted.
 */
static unsigned long g_thunk_hits[THUNK_MAX];

/*
 * Per-guest-entry-point call counts, for naming the HOT GUEST BODY behind a
 * slow window -- the level-build frames dispatch ~460k guest-to-guest calls
 * each (4x a normal frame's 110k), and the imports are not it, so the cost is
 * inside a recompiled body the ring cannot see.
 *
 * Armed by X2_HOTEP=<n> where n is the number of entry points to track: a
 * direct-mapped hash on the ENTRY POINT (key 0 = empty), so the dispatch path
 * pays one compare-and-increment against a table that never rehashes. The
 * collision check keeps each bucket honest: if a distinct EP hashes to an
 * occupied slot we record the collision once and stop counting NEW keys --
 * the table answers "which body is hot" correctly while it stays sparse,
 * which is exactly the load window. A full/overwritten table would read as
 * noise, so it refuses instead of guessing.
 *
 * The read side (x86_hotep_sorted) mirrors the thunk probe: per-interval
 * deltas, decoded to module+name by the heartbeat.
 */
#define HOTEP_MAX 4096
static uint32_t      g_hotep_key[HOTEP_MAX];
static unsigned long g_hotep_n[HOTEP_MAX];
static unsigned long long g_hotep_ns[HOTEP_MAX];
static unsigned      g_hotep_cap;       /* tracked EPs; 0 = probe unarmed */
static unsigned      g_hotep_collisions;

void x86_hotep_arm(const char *arg)
{
    unsigned long want = arg ? strtoul(arg, NULL, 10) : 0;
    g_hotep_cap = want > HOTEP_MAX ? HOTEP_MAX : (unsigned)want;
    g_hotep_collisions = 0;
    g_probe_time = want != 0;
    if (g_hotep_cap)
        fprintf(stderr, "[HOTEP] armed for the top %u guest entry points.\n",
                g_hotep_cap);
}

static inline void hotep_count(uint32_t ep, unsigned long long ns)
{
    unsigned long h;
    if (!g_hotep_cap) return;
    h = ((ep * 2654435761u) >> 8) % g_hotep_cap;
    if (!g_hotep_key[h]) { g_hotep_key[h] = ep; g_hotep_n[h] = 1;
                           g_hotep_ns[h] = ns; return; }
    if (g_hotep_key[h] == ep) { g_hotep_n[h]++; g_hotep_ns[h] += ns; return; }
    if (g_hotep_collisions < 16) g_hotep_collisions++;
}

/* The context of the callback currently executing, for x86_callback_ctx. A
   native class's hooks are shared C functions -- what distinguishes one
   class's getClassMetaSafe from another's is the synthetic address the guest
   called, so the dispatcher hands that identity to the callee. */
static void *g_cb_ctx;

/*
 * A thunk is created whether or not the import is actually implemented, because
 * the generated stubs are weak symbols and an implemented one cannot be told
 * from an unimplemented one by address.
 *
 * That is safe only because of the cycle-break in x86_import_call below. An
 * IMPLEMENTED stub does the work and never looks at its slot. An UNIMPLEMENTED
 * one dispatches through its slot -- which now holds this thunk, which calls
 * the stub, which dispatches through the slot... The first version of this
 * recursed until the stack died, and the comment here claimed it would "report
 * by name and stop". It did not. Wanting a design to be safe is not the same as
 * it being safe, so the break is explicit and tested rather than argued.
 */
uint32_t x86_native_thunk(const char *mod, const char *sym)
{
    X86Module *m;
    int i;
    for (m = g_head; m; m = m->next) {
        for (i = 0; i < m->nimports; i++) {
            const X86Import *im = &m->imports[i];
            if (strcasecmp(im->mod, mod) != 0 || strcmp(im->sym, sym) != 0)
                continue;
            if (g_nthunk == THUNK_MAX) return 0;
            g_thunk[g_nthunk].stub = im->stub;
            g_thunk[g_nthunk].mod = im->mod;
            g_thunk[g_nthunk].sym = im->sym;
            g_nthunk++;
            return THUNK_BASE + (uint32_t)(g_nthunk - 1) * 16u;
        }
    }
    return 0;
}

/*
 * A synthetic guest address for a native C function that is NOT an import.
 *
 * The thunk range above exists so guest code can call native implementations of
 * things it imports. Substituting an engine class through ARK needs the same
 * trick for a different reason: libIGCore is handed function POINTERS at
 * registration (getClassMetaSafe, retrieveVTablePointer, arkRegisterInitialize)
 * and calls them back later, and every slot of the class's vtable is a pointer
 * the engine will dispatch through. Those must be addresses the guest can call,
 * and a host function pointer is 64 bits and in the wrong address space.
 *
 * Same table, same dispatch, same ring entries -- the only difference is that
 * the slot is claimed directly rather than found by import name. `owner` and
 * `name` are what a boundary-ring line or a fault report will say, so they are
 * required: an anonymous callback is one that cannot be identified in the very
 * report that needs to name it.
 */
uint32_t x86_native_callback(void (*fn)(CPU *), const char *owner,
                             const char *name, void *ctx)
{
    if (!fn || !owner || !name) {
        fprintf(stderr, "x86_native_callback: refusing to register an "
                        "unnamed or NULL callback (fn=%p owner=%s name=%s)\n",
                (void *)fn, owner ? owner : "(null)", name ? name : "(null)");
        abort();
    }
    if (g_nthunk == THUNK_MAX) {
        fprintf(stderr, "x86_native_callback: the %d-entry synthetic address "
                        "table is full; %s::%s cannot be given a guest "
                        "address. Raise THUNK_MAX.\n",
                THUNK_MAX, owner, name);
        abort();
    }
    g_thunk[g_nthunk].stub = fn;
    g_thunk[g_nthunk].mod = owner;
    g_thunk[g_nthunk].sym = name;
    g_thunk[g_nthunk].ctx = ctx;
    g_nthunk++;
    return THUNK_BASE + (uint32_t)(g_nthunk - 1) * 16u;
}

void *x86_callback_ctx(void) { return g_cb_ctx; }

/*
 * Entry points of a module this host implements but NOTHING statically imports.
 *
 * x86_native_thunk above answers by searching the mapped modules' IMPORT
 * tables, which is right for a symbol some module links against -- and useless
 * for one the guest resolves at run time. XMen2.exe builds the path to
 * dinput8.dll from GetSystemDirectoryA, LoadLibraryAs it and asks for
 * DirectInput8Create by name; no import table mentions either, so there was
 * nothing for GetProcAddress to find and input was disabled wholesale
 * (issue #32).
 *
 * Registering here is also what makes "does this host implement that module"
 * answerable from ONE place: LoadLibraryA used to consult a hand-written list
 * of module names, which is a second source of truth that drifts from the set
 * of functions actually implemented.
 */
#define NATIVE_EXPORT_MAX 32
static struct {
    const char *mod, *sym;
    uint32_t    addr;
} g_nexport[NATIVE_EXPORT_MAX];
static int g_nnexport;

void x86_native_export(const char *mod, const char *sym, void (*fn)(CPU *))
{
    if (g_nnexport == NATIVE_EXPORT_MAX) {
        fprintf(stderr, "x86_native_export: the %d-entry table is full; "
                        "%s!%s cannot be published.\n",
                NATIVE_EXPORT_MAX, mod, sym);
        abort();
    }
    g_nexport[g_nnexport].mod = mod;
    g_nexport[g_nnexport].sym = sym;
    g_nexport[g_nnexport].addr = x86_native_callback(fn, mod, sym, NULL);
    g_nnexport++;
}

uint32_t x86_native_export_addr(const char *mod, const char *sym)
{
    int i;
    if (!mod || !sym) return 0;
    for (i = 0; i < g_nnexport; i++)
        if (strcasecmp(g_nexport[i].mod, mod) == 0
            && strcmp(g_nexport[i].sym, sym) == 0)
            return g_nexport[i].addr;
    return 0;
}

int x86_native_module_implemented(const char *mod)
{
    int i;
    if (!mod) return 0;
    for (i = 0; i < g_nnexport; i++)
        if (strcasecmp(g_nexport[i].mod, mod) == 0) return 1;
    return 0;
}

void x86_native_export_report(void)
{
    int i;
    if (!g_nnexport) {
        printf("  native exports: none registered -- no module is offered to "
               "LoadLibraryA beyond the ones this host maps.\n");
        return;
    }
    printf("  native exports (resolvable by LoadLibraryA + GetProcAddress):\n");
    for (i = 0; i < g_nnexport; i++)
        printf("        %-14s %-24s 0x%08x\n", g_nexport[i].mod,
               g_nexport[i].sym, g_nexport[i].addr);
}

/* Which import a thunk address belongs to, for diagnostics. A thunk that is
   DEREFERENCED rather than called means the import is data, not a function --
   and a thunk cannot serve data, so that import needs a real value. */
const char *x86_thunk_name(uint32_t addr, const char **mod)
{
    uint32_t i;
    if (addr < THUNK_BASE || addr >= THUNK_BASE + (uint32_t)THUNK_MAX * 16u)
        return NULL;
    i = (addr - THUNK_BASE) / 16u;
    if ((int)i >= g_nthunk) return NULL;
    *mod = g_thunk[i].mod;
    return g_thunk[i].sym;
}

static int thunk_call(uint32_t addr, CPU *C)
{
    uint32_t i, in;
    if (addr < THUNK_BASE || addr >= THUNK_BASE + (uint32_t)THUNK_MAX * 16u)
        return 0;
    i = (addr - THUNK_BASE) / 16u;
    if ((int)i >= g_nthunk || !g_thunk[i].stub) return 0;
    in = C->esp;
    g_thunk_hits[i]++;
    g_sample_ep = addr;
    {
        void *save = g_cb_ctx;
        g_cb_ctx = g_thunk[i].ctx;
        if (g_probe_time) span_push();
        g_thunk[i].stub(C);
        if (g_probe_time) g_host_import_ns += span_pop();
        g_cb_ctx = save;
    }
    ring_note(g_thunk[i].sym, addr, 0, in, C->esp, 0);
    /* Imports are recorded TOO. A hand-written stub has to pop its own
       arguments the way the __stdcall function it replaces does, and getting
       that wrong shifts the guest stack by a word -- the exact failure the
       ring above was built to make visible, and the one path the stack check
       could not see, because this returns before the dispatch recorder. */
    if (g_sc_armed && g_sc_out) {
        g_sc_records++;
        fprintf(g_sc_out, "IMPORT:%s %08x %08x %08x\n",
                g_thunk[i].sym ? g_thunk[i].sym : "?", addr, in, C->esp);
    }
    return 1;
}

/* ---- the boundary ring -------------------------------------------------
 *
 * The hosted build has one of these (src/x86watch.c) and the native build
 * needed its own for the same reason: a snapshot at the failure says where
 * execution ended up, not how it got there.
 *
 * It records ESP on both sides of every crossing, because the failure this was
 * built for is an ESP imbalance -- a hand-written import that pops the wrong
 * number of arguments shifts the guest stack by a word, and the damage appears
 * at some later RET that picks up the wrong word entirely. The imbalance is
 * invisible in a backtrace and obvious in a column of ESP values.
 */
/* Large, because with X86_NATIVE_TRACE every body entry and exit lands here
   and 96 entries covers a few microseconds of startup. It is a static array in
   a diagnostic build; the memory is not worth economising. */
/*
 * X2_ARGS is a TRACE-BUILD instrument. In an ordinary build it printed nothing
 * at all -- no banner, no report, no refusal -- so a run asking "is this
 * function ever entered" came back silent, and silence reads as "never
 * entered" when it actually means "never watched". This says which it is, at
 * STARTUP rather than at exit, because the answer changes whether the run is
 * worth waiting for.
 */
void x86_args_build_check(void)
{
    const char *e = getenv("X2_ARGS");
    if (!e || !*e) return;
#ifndef X86_NATIVE_TRACE
    fprintf(stderr,
        "[ARGS] X2_ARGS=%s is set and THIS BUILD CANNOT HONOUR IT. The watch "
        "sits on the body-entry hook, which only exists with\n"
        "       cmake -S . -B scratch/build-native -DX2_NATIVE_TRACE=ON\n"
        "       NOTHING will be watched in this run, and its silence about "
        "those entry points means nothing.\n", e);
#else
    fprintf(stderr, "[ARGS] X2_ARGS=%s -- this is a trace build, so the watch "
                    "is live.\n", e);
#endif
}

#ifdef X86_NATIVE_TRACE
#define RING 8192
#else
#define RING 96
#endif
/* `base` distinguishes the two address spaces this ring records. Host-side
   crossings note a MAPPED address (base 0); the per-body trace hook is called
   from generated code, which only knows its own LINKED entry point, so it also
   notes the module's runtime base. Without that the dump decoded a linked ep as
   a mapped one and confidently attributed libIGCore functions to libIGUtils --
   an instrument reporting the wrong module is worse than one reporting none. */
static struct {
    const char *what;
    uint32_t    addr, base, esp_in, esp_out;
    /*
     * The caller's return address, for a body ENTRY -- 0 where there is none
     * to record.
     *
     * Without it the ring can show a two-body loop and say nothing about who
     * is running it, because the loop itself never crosses a boundary: issue
     * #35 sat on "something calls the frame timer forever" for a session
     * because the only thing the ring named was the timer. It is the one word
     * the generated prologue already has (`_retaddr`), so it costs a store.
     */
    uint32_t    ret;
    unsigned    repeat;
} g_ring[RING];
/* unsigned long, not unsigned: a run that reaches the main loop passes 2^32
   crossings in a trace build, and a counter that wraps would have the
   heartbeat report a negative delta as an enormous positive one. */
static unsigned long g_ring_n;

/*
 * Consecutive identical crossings collapse into one entry with a count.
 *
 * Without it a hot leaf drowns the ring: one four-instruction index helper
 * called in a loop filled all 96 slots with the same line, and the history
 * that mattered -- what happened BEFORE the imbalance -- had already scrolled
 * out. Capping the boring case rather than the interesting one is the whole
 * point of a ring this size.
 */
unsigned long x86_crossings(void) { return g_ring_n; }
unsigned int x86_thunk_count(void) { return (unsigned int)g_nthunk; }

/*
 * Per-interval import probe: the N most-called host imports between two reads.
 *
 * The heartbeat asks for this every period. The caller keeps a snapshot of the
 * cumulative counts and subtracts -- same torn-read trade as every counter the
 * heartbeat reads -- and gets back, for the interval, which imports the guest
 * called the most and how often. THAT is the load-window question: the ring
 * said the build frames cross the boundary 400k+ times/frame and nothing else,
 * and this is the probe that names the import behind it instead of guessing.
 *
 * Returns the number of imports written. Sorted by delta, descending, no
 * smoothing and no minimum -- the caller decides what to print.
 */
unsigned int x86_thunk_crossings_sorted(unsigned long *snapshot,
                                        const char **mod, const char **sym,
                                        unsigned long *hits, unsigned int cap)
{
    unsigned int n = 0;
    int i;
    for (i = 0; i < g_nthunk; i++) {
        unsigned long d = g_thunk_hits[i] - snapshot[i];
        int j;
        if (!d) continue;
        if (n == cap && d <= hits[cap - 1]) continue;  /* no room this round */
        if (n == cap)
            n--;                                        /* drop the tail */
        for (j = (int)n - 1; j >= 0 && d > hits[j]; j--) {
            mod[j + 1] = mod[j]; sym[j + 1] = sym[j]; hits[j + 1] = hits[j];
        }
        mod[j + 1] = g_thunk[i].mod; sym[j + 1] = g_thunk[i].sym;
        hits[j + 1] = d;
        n++;
    }
    for (i = 0; i < g_nthunk; i++) snapshot[i] = g_thunk_hits[i];
    return n;
}

/*
 * The hot guest bodies, by ENTRY-POINT deltas since the last read.
 *
 * The heartbeat decodes each returned EP back to module+name via
 * x86_module_for / x86_native_name_at. Sorted by WALL TIME (ns), with the
 * call count alongside: the load window's ~460k dispatches/frame is the cost
 * only if the time inside them is; a cheap hot leaf tops a count sort and
 * buries the body that actually owns the 250ms. Returns 0 when unarmed.
 */
unsigned int x86_hotep_sorted(uint32_t *ep, unsigned long long *ns,
                              unsigned long *hits, unsigned int cap)
{
    unsigned int n = 0;
    unsigned i;
    for (i = 0; i < g_hotep_cap && n < cap; i++) {
        unsigned long long d;
        int j;
        if (!g_hotep_key[i]) continue;
        d = g_hotep_ns[i];
        for (j = (int)n - 1; j >= 0 && d > ns[j]; j--) {
            ep[j + 1] = ep[j]; ns[j + 1] = ns[j]; hits[j + 1] = hits[j];
        }
        ep[j + 1] = g_hotep_key[i]; ns[j + 1] = d; hits[j + 1] = g_hotep_n[i];
        n++;
    }
    return n;
}

unsigned int x86_hotep_collisions(void) { return g_hotep_collisions; }

/* ---- the sampling profiler ----------------------------------------------
 *
 * WHY IT EXISTS. The hotep probe cannot name a load-window hotspot: the level
 * build dispatches ~460k DISTINCT entry points, a fixed hash table refuses
 * everything that collides, and the refused ones include the hot ones -- the
 * top-5 came back as 0.0ms in 1-2 dispatches while the interval's guest total
 * was 1400ms. A sampler does not need to see every EP: it reads the CURRENT
 * guest body every few ms and histograms the SAMPLES, so a body that runs a
 * lot is sampled a lot, whatever else ever ran.
 *
 * "Current guest body" is g_sample_ep, one store on every dispatch (both
 * guest bodies and import stubs), written by the thread running guest code
 * and read by the sampler thread. A 32-bit aligned store/load on x86 is
 * atomic, so a sample is never torn; it can only be stale by one dispatch,
 * which is exactly what a sample is supposed to be.
 *
 * Armed by X2_PROFILE=<period-ms>; reports at the end of the run through
 * x2_interrupt_reports (never only at a crash). The histogram prints its
 * sample total as the denominator, so "0 samples" is distinguishable from
 * "the probe never ran".
 */
#define PROFILE_MAX 1024
#define PROFILE_TOP 14
static struct {
    uint32_t ep;
    unsigned long n;
} g_profile[PROFILE_MAX];
static int g_profile_n;
static unsigned long g_profile_dropped, g_profile_total;

static void *profiler_thread(void *arg)
{
    long period_ns = (long)(intptr_t)arg * 1000000L;
    struct timespec req = { 0, (long)period_ns % 1000000000L };
    req.tv_sec = period_ns / 1000000000L;
    for (;;) {
        while (nanosleep(&req, &req) != 0 && errno == EINTR) ;
        if (x2_report_now) return NULL;   /* let the heartbeat print the report */
        {
            uint32_t ep = g_sample_ep;
            int i;
            if (!ep) continue;
            g_profile_total++;
            for (i = 0; i < g_profile_n; i++)
                if (g_profile[i].ep == ep) { g_profile[i].n++; break; }
            if (i == g_profile_n) {
                if (g_profile_n < PROFILE_MAX) {
                    g_profile[g_profile_n].ep = ep;
                    g_profile[g_profile_n].n = 1;
                    g_profile_n++;
                } else {
                    g_profile_dropped++;
                }
            }
        }
    }
}

void x86_profiler_report(void)
{
    int i, j;
    unsigned long shown = 0;
    printf("\n[PROF] %lu sample(s) of the running guest body", g_profile_total);
    if (g_profile_dropped)
        printf(" (%lu dropped past the %d-entry histogram)",
               g_profile_dropped, PROFILE_MAX);
    printf(", by entry point:\n");
    /* Top PROFILE_TOP by count, insertion-sorted like the hotep reader. */
    {
        uint32_t e[PROFILE_TOP]; unsigned long c[PROFILE_TOP];
        int n = 0;
        for (i = 0; i < g_profile_n && n < PROFILE_TOP; i++) {
            for (j = n - 1; j >= 0 && g_profile[i].n > c[j]; j--) {
                e[j + 1] = e[j]; c[j + 1] = c[j];
            }
            e[j + 1] = g_profile[i].ep; c[j + 1] = g_profile[i].n;
            if (n < PROFILE_TOP) n++;
        }
        for (i = 0; i < n; i++) {
            const char *nm = x86_native_name_at(e[i]);
            X86Module *m = x86_module_for(e[i]);
            shown += c[i];
            printf("  %5.1f%% %lu  %s0x%08x (%s%s)\n",
                   100.0 * (double)c[i] / (g_profile_total ? g_profile_total : 1),
                   c[i], nm ? "" : "unresolved ", e[i],
                   nm ? nm : (m ? m->name : "???"),
                   (nm || !m) ? "" : " +offset");
        }
    }
    if (shown < g_profile_total)
        printf("  ... the other %lu sample(s) spread over %d more entry "
               "point(s)\n", g_profile_total - shown,
               g_profile_n > PROFILE_TOP ? g_profile_n - PROFILE_TOP : 0);
}

void x86_profiler_start(const char *arg)
{
    long period_ms;
    pthread_t th;
    char *end = NULL;
    if (!arg || !*arg) return;
    period_ms = strtol(arg, &end, 10);
    if (period_ms <= 0 || (end && *end)) {
        fprintf(stderr, "X2_PROFILE=%s is not a positive period in ms; the "
                        "sampler did not start.\n", arg ? arg : "");
        return;
    }
    if (pthread_create(&th, NULL, profiler_thread,
                       (void *)(intptr_t)period_ms) != 0) {
        fprintf(stderr, "X2_PROFILE: could not start the sampler thread; "
                        "nothing will be sampled.\n");
        return;
    }
    pthread_detach(th);
    fprintf(stderr, "X2_PROFILE=%ldms: sampling the running guest body every "
                    "%ld ms; the histogram prints at the end of the run.\n",
            period_ms, period_ms);
}

/*
 * Wall-time split between host import stubs and guest bodies since the last
 * read -- the number the crossing COUNTS cannot give. Armed with X2_HOTEP;
 * unarmed, zeroes are returned and the heartbeat prints nothing.
 *
 * The split answers the load-window question in one line: "500k crossings per
 * frame" says the boundary is busy but not who paid for the 250ms. If the
 * host-import share is small, the cost is inside the recompiled guest bodies
 * (a translation/algorithm issue); if it is large, the cost is in the stubs
 * this host wrote (ReadFile, the heap, threads) and is ours to fix directly.
 */
void x86_probe_time_delta(unsigned long long *host_import_ns,
                          unsigned long long *guest_body_ns)
{
    static unsigned long long phost, pguest;
    if (!g_probe_time) {
        *host_import_ns = *guest_body_ns = 0;
        return;
    }
    *host_import_ns = g_host_import_ns - phost;
    *guest_body_ns = g_guest_body_ns - pguest;
    phost = g_host_import_ns;
    pguest = g_guest_body_ns;
}

/*
 * The preemption point's budget and its action -- see X86_ENTER_FN in x86rt.h
 * for why it lives in every body rather than at the dispatch boundary.
 *
 * The initial value is the default quantum. X2_QUANTUM=0 makes
 * guest_quantum_size() enormous, so the next re-arm effectively switches this
 * off, which is exactly what the control is meant to do.
 */
unsigned long x86_preempt_budget = 20000;
void x86_preempt_now(void)
{
    x86_preempt_budget = guest_quantum_size();
    guest_quantum();
}

const char *x86_crossings_what(void)
{
#ifdef X86_NATIVE_TRACE
    return "body entries and exits";
#else
    return "host-boundary crossings only (this is not a trace build, so "
           "guest-to-guest calls are invisible here)";
#endif
}

static void ring_note(const char *what, uint32_t addr, uint32_t base,
                      uint32_t in, uint32_t out, uint32_t ret)
{
    unsigned long i;
    if (g_ring_n) {
        i = (g_ring_n - 1) % RING;
        if (g_ring[i].addr == addr && g_ring[i].base == base
            && g_ring[i].esp_in == in && g_ring[i].esp_out == out
            && g_ring[i].ret == ret) {
            g_ring[i].repeat++;
            return;
        }
    }
    i = g_ring_n++ % RING;
    g_ring[i].what = what;
    g_ring[i].addr = addr;
    g_ring[i].base = base;
    g_ring[i].esp_in = in;
    g_ring[i].esp_out = out;
    g_ring[i].ret = ret;
    g_ring[i].repeat = 0;
}

/* These two live OUTSIDE the trace guard: the trace watch is not their
   only user any more -- X2_EPCOUNT decodes script names with them in an
   ordinary build, at report time -- and a second copy would be a second
   thing to get wrong. */
/*
 * Decoding a word as a STRING, and the two ways that goes wrong.
 *
 * Most of the arguments worth watching are char* -- a library name, a format
 * string, a class name -- and the watch printed them as hex, so answering
 * "which library failed to load" meant reading guest memory by hand after the
 * process was already gone. So the watch decodes them.
 *
 * A wild word dereferenced would take the process down inside the diagnostic,
 * which is the worst possible place for it, so every page is PROBED before it
 * is read. The probe is a write() of the range to /dev/null: the kernel does
 * the access check and answers EFAULT instead of raising SIGSEGV, so an
 * unmapped word costs an errno rather than the run.
 *
 * The first version bounded the read to mapped module images and live guest
 * heap blocks instead, and that was too narrow to answer the question it was
 * built for: the string it needed to read (a library name held by the game's
 * OWN CRT heap, which this host does not track block by block) fell outside
 * both and printed nothing. The probe has no such blind spot -- it asks the
 * kernel what is readable, which is the only authority on it.
 *
 * The second failure is the opposite one: printing 40 bytes of a struct as if
 * they were text. So it demands the whole prefix be printable, and stops at
 * the first byte that is not.
 */
/*
 * THE OLD PROBE VALIDATED NOTHING. It wrote the range to /dev/null and took a
 * full-length return as proof the memory was readable -- but Linux's null
 * device never copies from the buffer, so write() succeeds for a wild pointer
 * and the probe answered "readable" for every address. The decoder then
 * dereferenced it, and the report died inside the diagnostic with the faulting
 * address in hand (I048).
 *
 * process_vm_readv is what the rest of this file already uses to read guest
 * memory without risking a fault -- see x86_peek. It COPIES, so an unmapped
 * page comes back as an error instead of a signal.
 */
static int args_string_at(uint32_t a, char *out, size_t cap)
{
    uint32_t i;

    if (a < 0x1000u) return 0;
    for (i = 0; i + 1 < cap; i++) {
        unsigned char c;
        if (!x86_peek(a + i, &c, 1)) break;       /* unmapped: stop, no fault */
        if (c == 0) { out[i] = 0; return i > 0; }
        if (c == '\n' || c == '\t') { out[i] = ' '; continue; }
        if (c < 0x20 || c > 0x7e) return 0;
        out[i] = (char)c;
    }
    /* Ran out of buffer, or off the end of what is mapped, with everything so
       far printable. Shown TRUNCATED rather than dropped: a long format string
       is exactly the kind of argument this watch exists to read, and dropping
       it printed nothing at all. The floor keeps three stray printable bytes
       from being announced as text. */
    if (i >= 8) { memcpy(out + i - 3, "...", 4); return 1; }
    return 0;
}

#ifdef X86_NATIVE_TRACE
/*
 * Entry and exit of every recompiled body.
 *
 * Exit records the ESP the body is LEAVING with, so a mismatched prologue and
 * epilogue shows up as a body whose exit ESP is not its entry ESP plus the
 * bytes its RET should have popped. That is the failure this exists to find,
 * and it is invisible without it: an ordinary guest-to-guest call is a direct
 * C call and crosses no boundary at all.
 */
/*
 * Argument watch: X2_ARGS=0x10056330,0x1005ae50
 *
 * The native counterpart of the hosted build's X2_WATCH (I019), which the
 * native build did not have -- so "how many times was it called" was
 * answerable (the reached set) and "what was it called WITH" was not.
 *
 * On entry it prints ECX (the __thiscall `this`) and the first four stack
 * words above the return address; on exit, EAX. It CANNOT know a function's
 * real argument count -- nothing here does, which is why the export shims are
 * built not to need it -- so it prints a fixed four and says so. Words beyond
 * the real count are whatever the caller's frame holds, not arguments.
 */
/*
 * Capping: by NOVELTY, not by count.
 *
 * A flat cap ("print the first 8 calls") and no cap at all fail the same
 * function called from a loop -- the first drowns the interesting call sites
 * under the boring one, the second drowns the log. What identifies a caller is
 * the RETURN ADDRESS, so the cap is per (entry point, return address): every
 * distinct call site is always printed the first time it appears, and repeats
 * from a site already seen stop after X2_ARGS_MAX (default 8).
 *
 * The suppressed calls are COUNTED, per site, and printed in the report. A
 * watch that silently dropped them would make one call site indistinguishable
 * from a million, which is the whole question when the symptom is a spin.
 */
#define ARGS_MAX   16
#define ARGS_SITES 24
static struct ArgsWatch {
    uint32_t      ep;
    char          mod[24];               /* "" = any module (see args_init) */
    unsigned long calls;                 /* entries seen for this ep */
    int           nsites;
    int           lost;                  /* distinct sites past ARGS_SITES */
    unsigned long lost_calls;
    struct { uint32_t ret; unsigned long n, shown; } site[ARGS_SITES];
    int           shown;                 /* the last entry printed (see below) */
} g_args[ARGS_MAX];
static int g_args_n = -1, g_args_hits, g_args_cap = 8, g_args_printed;

/* One line per word that decoded, and nothing at all when none did -- so a
   silent watch means "no argument pointed at a string I could reach", not
   "there were no strings". */
static void args_print_strings(const char *tag, const uint32_t *w, int n)
{
    char buf[120];
    int i;
    for (i = 0; i < n; i++)
        if (args_string_at(w[i], buf, sizeof buf))
            fprintf(stderr, "[ARGS]      %s[%d] 0x%08x -> \"%s\"\n",
                    tag, i, w[i], buf);
}

static void args_init(void)
{
    const char *e = getenv("X2_ARGS"), *c = getenv("X2_ARGS_MAX");
    char buf[256], *p, *save;
    g_args_n = 0;
    if (c && *c) g_args_cap = (int)strtol(c, NULL, 0);
    if (!e || !*e) return;
    snprintf(buf, sizeof buf, "%s", e);
    /*
     * "0x10068da0" or "libCriMovie:0x100026f0".
     *
     * The qualifier is not decoration. Every libIG*.dll and libCriMovie is
     * LINKED for 0x10000000, so a bare guest address names a function in each
     * of nineteen modules at once -- and the watch printed libIGSg's
     * igMatrixObjectPool::getClassMeta for a run that was asking about
     * libCriMovie's movie init, with nothing in the output to say so. An
     * unqualified address still matches any module, because that is what the
     * exe's addresses need and what every existing use expects.
     */
    for (p = strtok_r(buf, ",", &save); p && g_args_n < ARGS_MAX;
         p = strtok_r(NULL, ",", &save)) {
        char *colon = strchr(p, ':');
        struct ArgsWatch *w = &g_args[g_args_n++];
        memset(w->mod, 0, sizeof w->mod);
        if (colon) {
            *colon = 0;
            snprintf(w->mod, sizeof w->mod, "%s", p);
            p = colon + 1;
        }
        w->ep = (uint32_t)strtoul(p, NULL, 0);
    }
    {   int i;
        for (i = 0; i < g_args_n; i++)
            fprintf(stderr, "[ARGS]   0x%08x in %s\n", g_args[i].ep,
                    g_args[i].mod[0] ? g_args[i].mod
                                     : "ANY module (every libIG*.dll is linked "
                                       "for 0x10000000, so this may match "
                                       "several -- qualify it as mod:0xADDR)");
    }
    fprintf(stderr, "[ARGS] watching %d entry point(s); ECX and 4 stack words "
                    "per call. The real argument count is unknown, so trailing "
                    "words may be the caller's frame rather than arguments.\n"
                    "[ARGS] every distinct return address is printed once; "
                    "repeats from a known one stop after %d (X2_ARGS_MAX) and "
                    "are counted in the report.\n",
            g_args_n, g_args_cap);
}

/* The module a MAPPED base belongs to, or NULL. */
static const char *args_module_of(uint32_t base)
{
    X86Module *m;
    for (m = x86_modules(); m; m = m->next)
        if (*m->base == base) return m->name;
    return NULL;
}

/* A qualifier matches the module's file name up to its extension, so
   "libCriMovie" matches "libCriMovie.dll" and "XMen2" matches "XMen2.exe". */
static int args_mod_matches(const char *want, const char *have)
{
    size_t n;
    if (!want || !*want) return 1;
    if (!have) return 0;
    n = strlen(want);
    return strncasecmp(have, want, n) == 0 && (have[n] == 0 || have[n] == '.');
}

static struct ArgsWatch *args_watched(uint32_t ep, uint32_t base)
{
    int i;
    if (g_args_n < 0) args_init();
    for (i = 0; i < g_args_n; i++)
        if (g_args[i].ep == ep
            && args_mod_matches(g_args[i].mod, args_module_of(base)))
            return &g_args[i];
    return NULL;
}

/* Returns 1 if this call should be printed, and says whether the call site is
   one never seen before -- a new site is worth a marker in the line, because
   it is the only kind of line that can answer "who else calls this". */
static int args_should_print(struct ArgsWatch *w, uint32_t ret, int *is_new)
{
    int i;
    *is_new = 0;
    w->calls++;
    for (i = 0; i < w->nsites; i++)
        if (w->site[i].ret == ret) {
            w->site[i].n++;
            if ((long)w->site[i].shown >= g_args_cap) return 0;
            w->site[i].shown++;
            return 1;
        }
    if (w->nsites == ARGS_SITES) {       /* a blind spot, so it is reported */
        w->lost++;
        w->lost_calls++;
        return 0;
    }
    i = w->nsites++;
    w->site[i].ret = ret;
    w->site[i].n = w->site[i].shown = 1;
    *is_new = 1;
    return 1;
}

/* Reported at exit so a watch that never fired cannot be read as "it was
   called with nothing interesting". Per watched entry point, so one that was
   never entered is distinguishable from one that was -- and with the call
   sites and their counts, which is what turns "it spins" into "it spins from
   HERE". */
void x86_args_report(void)
{
    int i, j;
    if (g_args_n < 0) args_init();
    if (!g_args_n) return;
    if (!g_args_hits) {
        fprintf(stderr, "[ARGS] NONE of the %d watched entry point(s) was "
                        "entered -- this run says nothing about their "
                        "arguments.\n", g_args_n);
        return;
    }
    fprintf(stderr, "[ARGS] %d call(s), %d printed, across %d watched entry "
                    "point(s):\n", g_args_hits, g_args_printed, g_args_n);
    for (i = 0; i < g_args_n; i++) {
        struct ArgsWatch *w = &g_args[i];
        if (!w->calls) {
            fprintf(stderr, "[ARGS]   0x%08x  NEVER ENTERED\n", w->ep);
            continue;
        }
        fprintf(stderr, "[ARGS]   0x%08x  %lu call(s) from %d call site(s):\n",
                w->ep, w->calls, w->nsites);
        for (j = 0; j < w->nsites; j++)
            fprintf(stderr, "[ARGS]       ret to %08x  x%lu\n",
                    w->site[j].ret, w->site[j].n);
        if (w->lost)
            fprintf(stderr, "[ARGS]       ... and %lu call(s) from call sites "
                            "BEYOND the %d this watch can hold -- those "
                            "addresses were not recorded.\n",
                    w->lost_calls, ARGS_SITES);
    }
}

/* `w->shown` is set by an entry that printed and read by the matching exit: an
   exit line for a call whose entry was suppressed is noise with nothing to
   attach to. Per watched entry point, so a watched function calling another
   watched function pairs correctly; direct recursion of ONE watched function
   can still pair an exit with the wrong entry, and only the eax value is at
   stake there. */
void x86_trace_enter(uint32_t ep, uint32_t base, const CPU *C)
{
    struct ArgsWatch *w;
    ring_note("enter", ep, base, C->esp, C->esp, RD32(C->esp));
    if ((w = args_watched(ep, base)) != NULL) {
        /* Resolve through the module that HAS this base, never by assuming a
           preferred address: the exe is linked for 0x400000, not 0x10000000,
           and hardcoding one is how a report names the wrong function. */
        X86Module *m;
        const char *nm = NULL;
        uint32_t ret = RD32(C->esp);
        int is_new;
        g_args_hits++;
        w->shown = args_should_print(w, ret, &is_new);
        if (!w->shown) return;
        for (m = x86_modules(); m; m = m->next)
            if (*m->base == base) { nm = x86_native_name_at(base + (ep - m->preferred)); break; }
        g_args_printed++;
        fprintf(stderr, "[ARGS] -> 0x%08x %-38s ecx %08x  args %08x %08x "
                        "%08x %08x  (ret to %08x%s)\n",
                ep, nm ? nm : "", C->ecx,
                RD32(C->esp + 4), RD32(C->esp + 8),
                RD32(C->esp + 12), RD32(C->esp + 16), ret,
                is_new ? ", NEW call site" : "");
        /* The callee-saved four, at the moment of entry -- so they are still
           the CALLER's. A caller that indexes off EDI or EBP across a call
           (issue #36) cannot be diagnosed from the arguments alone: the
           question there is whether the value the caller is still using is
           the one it had, and only the register file answers it. */
        fprintf(stderr, "[ARGS]      caller-live  ebx %08x  ebp %08x  "
                        "esi %08x  edi %08x\n",
                C->ebx, C->ebp, C->esi, C->edi);
        {   /* ecx first: a __thiscall's `this` is not a string, but the four
               stack words are as likely to be char* as anything else. */
            uint32_t words[5];
            words[0] = C->ecx;
            words[1] = RD32(C->esp + 4);
            words[2] = RD32(C->esp + 8);
            words[3] = RD32(C->esp + 12);
            words[4] = RD32(C->esp + 16);
            args_print_strings("arg", words, 5);
        }
        /* X2_PEEK at every watched call, not only at the fault. A dump taken
           once at the end shows the wreckage; what identifies WHICH call broke
           an invariant is the same addresses before and after each one. */
        x86_peek_report();
    }
}

void x86_trace_exit(uint32_t ep, uint32_t base, const CPU *C)
{
    ring_note("exit", ep, base, C->esp, C->esp, 0);
    struct ArgsWatch *w = args_watched(ep, base);
    if (w && w->shown) {
        /* edx as well as eax: a 64-bit return comes back in EDX:EAX, and a
           watch that prints only the low half reports a saturating or
           truncated 64-bit value as a plausible small one. edx is meaningless
           for a 32-bit return, which is why it is labelled rather than merged
           into one number. */
        fprintf(stderr, "[ARGS] <- 0x%08x  eax %08x  edx %08x  (as int64 %lld)\n"
                        "[ARGS]      returned-with ebx %08x  ebp %08x  "
                        "esi %08x  edi %08x\n",
                ep, C->eax, C->edx,
                (long long)(((uint64_t)C->edx << 32) | C->eax),
                C->ebx, C->ebp, C->esi, C->edi);
        x86_peek_report();
    }
}
#endif

/* ---- guest-memory peek ------------------------------------------------
 *
 * "What was actually in that slot when it died?" -- the question every
 * cross-module data bug reduces to, and one the ring and the reached set
 * cannot answer because both record control flow, not state.
 *
 *   X2_PEEK=libIGCore+0x15f3fc:1,0x0067f708:4
 *
 * A bare 0x… is a GUEST/mapped address as-is; <module>+0x… is an offset from
 * that module's LINKED base, resolved through wherever it actually got mapped,
 * which is the form a Ghidra address can be pasted into.
 *
 * The read goes through process_vm_readv rather than a dereference: this runs
 * from a SIGSEGV handler, where a second fault would be a silent recursive
 * crash and the report would be lost -- so an unreadable address has to come
 * back as an error value, not as a signal.
 */
/* One safe read; 0 on failure. Never dereferences -- see x86_peek_report. */
int x86_peek(uint32_t addr, void *dst, size_t n)
{
    struct iovec loc, rem;
    loc.iov_base = dst;                        loc.iov_len = n;
    rem.iov_base = (void *)(uintptr_t)addr;    rem.iov_len = n;
    return process_vm_readv(getpid(), &loc, 1, &rem, 1, 0) == (ssize_t)n;
}

int x86_peek32(uint32_t addr, uint32_t *out)
{
    return x86_peek(addr, out, sizeof *out);
}

static int peek_read(uint32_t addr, void *dst, size_t n)
{
    struct iovec loc, rem;
    loc.iov_base = dst;                        loc.iov_len = n;
    rem.iov_base = (void *)(uintptr_t)addr;    rem.iov_len = n;
    return process_vm_readv(getpid(), &loc, 1, &rem, 1, 0) == (ssize_t)n;
}

/* Print up to `max` bytes at addr as a C string. Says why it printed nothing
   rather than printing an empty pair of quotes, which reads as "the string is
   empty" when it usually means the address was wrong. */
static void peek_string(uint32_t addr, unsigned max)
{
    char s[129];
    unsigned i;
    if (max > sizeof s - 1) max = sizeof s - 1;
    for (i = 0; i < max; i++) {
        unsigned char c;
        if (!peek_read(addr + i, &c, 1)) {
            if (i == 0) { fprintf(stderr, "UNREADABLE (not mapped)\n"); return; }
            break;
        }
        if (!c) break;
        s[i] = (c >= 32 && c < 127) ? (char)c : '.';
    }
    s[i] = 0;
    if (!i) fprintf(stderr, "\"\" (empty: first byte is NUL)\n");
    else    fprintf(stderr, "\"%s\"%s\n", s, i == max ? " (truncated)" : "");
}

/*
 * X2_PEEK=<place>[:<how>],...
 *
 *   <place>  0xABS                  a guest/mapped address as-is
 *            <module>+0xOFF         offset from that module's LINKED base,
 *                                   resolved through where it actually mapped,
 *                                   so a Ghidra address can be pasted in
 *   <how>    1 | 2 | 4              that many bytes, as a number (default 4)
 *            s | sN                 a C string AT that address, N bytes max
 *            *s | *sN               follow the dword there, then the string
 *            dN                     N consecutive dwords
 *
 * The *s and dN forms exist because the 4-byte-only version cost one whole run
 * per guess: identifying a meta field's name meant re-running the game once
 * for every candidate offset (issue #16).
 *
 * Every read goes through process_vm_readv rather than a dereference: this runs
 * from a SIGSEGV handler, where a second fault would be a silent recursive
 * crash and the report would be lost.
 */
void x86_peek_report(void)
{
    const char *spec = getenv("X2_PEEK");
    /* 2 KB: a whole-object sweep is ~64 items and 512 bytes silently TRUNCATED
       the spec, so the tail of the sweep was simply not read. */
    char buf[2048], *p, *save;
    if (!spec || !*spec) return;
    snprintf(buf, sizeof buf, "%s", spec);
    {   /* banner once: this now runs per watched call, and repeating the
           spec every time would bury the values it exists to show */
        static int banner;
        if (!banner) { fprintf(stderr, "[PEEK] X2_PEEK=%s\n", spec); banner = 1; }
    }
    for (p = strtok_r(buf, ",", &save); p; p = strtok_r(NULL, ",", &save)) {
        char item[128], *colon, *plus, how[24] = "4";
        unsigned size = 4, count = 1, i;
        uint32_t addr = 0;
        int resolved = 0, str = 0, deref = 0;
        unsigned char val[8];
        snprintf(item, sizeof item, "%s", p);
        if ((colon = strrchr(item, ':')) != NULL) {
            snprintf(how, sizeof how, "%s", colon + 1);
            *colon = 0;
        }
        {   const char *h = how;
            if (*h == '*') { deref = 1; h++; }
            if (*h == 's') { str = 1; count = h[1] ? (unsigned)strtoul(h + 1, NULL, 0) : 48; }
            else if (*h == 'd') { count = h[1] ? (unsigned)strtoul(h + 1, NULL, 0) : 1; size = 4; }
            else if (deref) { str = 1; count = 48; }   /* bare '*' means *s */
            else {
                size = (unsigned)strtoul(h, NULL, 0);
                if (size != 1 && size != 2 && size != 4) {
                    fprintf(stderr, "[PEEK]   %s: '%s' is not a size (1,2,4), a "
                                    "string (s/sN/*s) or a dword run (dN) -- "
                                    "NOT read\n", item, how);
                    continue;
                }
            }
        }
        if ((plus = strchr(item, '+')) != NULL) {
            X86Module *m;
            *plus = 0;
            for (m = x86_modules(); m; m = m->next)
                if (strcasecmp(m->name, item) == 0
                    || (strncasecmp(m->name, item, strlen(item)) == 0
                        && strchr(m->name, '.') == m->name + strlen(item))) {
                    addr = *m->base + (uint32_t)strtoul(plus + 1, NULL, 0);
                    resolved = 1;
                    break;
                }
            if (!resolved) {
                fprintf(stderr, "[PEEK]   %s+%s: NO module of that name is "
                                "registered -- nothing was read\n", item, plus + 1);
                continue;
            }
            fprintf(stderr, "[PEEK]   %s+%s -> mapped 0x%08x: ", item, plus + 1, addr);
        } else {
            addr = (uint32_t)strtoul(item, NULL, 0);
            fprintf(stderr, "[PEEK]   0x%08x: ", addr);
        }
        if (deref) {
            if (!peek_read(addr, val, 4)) {
                fprintf(stderr, "UNREADABLE (not mapped)\n");
                continue;
            }
            addr = (uint32_t)(val[0] | val[1] << 8 | val[2] << 16 | (unsigned)val[3] << 24);
            fprintf(stderr, "-> 0x%08x ", addr);
            if (!addr) { fprintf(stderr, "(NULL, so no string to read)\n"); continue; }
        }
        if (str) { peek_string(addr, count); continue; }
        for (i = 0; i < count; i++) {
            if (!peek_read(addr + i * size, val, size)) {
                fprintf(stderr, "%sUNREADABLE (not mapped)", i ? " " : "");
                break;
            }
            if (size == 1)      fprintf(stderr, "%s0x%02x", i ? " " : "", val[0]);
            else if (size == 2) fprintf(stderr, "%s0x%04x", i ? " " : "",
                                        (unsigned)(val[0] | val[1] << 8));
            else fprintf(stderr, "%s0x%08x", i ? " " : "",
                         (unsigned)(val[0] | val[1] << 8 | val[2] << 16
                                    | (unsigned)val[3] << 24));
        }
        fputc('\n', stderr);
    }
}

/*
 * Everything the process knows, at any stop.
 *
 * The abort paths called only x86_ring_dump(), and abort() does not run atexit
 * handlers -- so the reached set and the argument watch were silent on exactly
 * the failures worth reporting. An instrument that goes quiet when the run
 * fails is not an instrument.
 */
void x86_diag_dump(void)
{
    /*
     * The THREAD table, on every stop path. It is registered with atexit()
     * too, and atexit does not run on abort() -- so at exactly the stops worth
     * reading (a stall, a missing import, a fault) the one fact that explains
     * a stalled run, "tid N is suspended and nobody resumed it", was silent.
     */
    /* The thread and critical-section reports are NOT printed here: they moved
       to x2_interrupt_reports, which runs on every ending rather than only on
       the ones that dump the ring. Printing them in both places would double
       every number on a killed run. */
    /* The multimedia timers, for the same reason: a stall whose cause is "the
       callback that would have ended this wait has never run" is invisible
       unless the fire count is printed where the stall is. */
    { extern void winmm_report(void); winmm_report(); }
    x86_peek_report();
    x86_reached_report();
#ifdef X86_NATIVE_TRACE
    x86_args_report();
#endif
    x86_ring_dump();
}

void x86_ring_dump(void)
{
    unsigned long n = g_ring_n < RING ? g_ring_n : RING, i;
    if (!g_ring_n) {
        fprintf(stderr, "[TRACE] the boundary ring is EMPTY: nothing crossed "
                        "between guest and host before this point.\n");
        return;
    }
    fprintf(stderr, "[TRACE] last %lu of %lu crossings (esp in -> out; a delta "
                    "that is not 4+4N for a stdcall import is the bug).\n"
                    "[TRACE] x86_return_to fired %lu time(s) -- a correct "
                    "translation should almost never need it:\n",
            n, g_ring_n, g_return_to_calls);
    for (i = g_ring_n - n; i < g_ring_n; i++) {
        unsigned k = i % RING;
        uint32_t a = g_ring[k].addr;
        /* The ring records the MAPPED address, but every module here is linked
           for 0x10000000 and relocated elsewhere, so a mapped address matches
           nothing in Ghidra, in docs/, or in a seed file. Print the guest
           address it corresponds to, and its name when one is known -- without
           this the reader has to redo the relocation arithmetic by hand for
           every line, which is how a 96-line ring stayed unread. */
        uint32_t b = g_ring[k].base;
        X86Module *m = NULL;
        uint32_t guest = a;
        const char *nm = NULL;
        if (b) {
            /* `a` is a LINKED entry point in the module whose runtime base is
               `b`. Resolve by base, never by treating the ep as an address. */
            for (m = x86_modules(); m; m = m->next) if (*m->base == b) break;
            nm = m ? x86_native_name_at(b + (a - m->preferred)) : NULL;
        } else {
            m = x86_module_for(a);
            guest = m ? m->preferred + (a - *m->base) : a;
            nm = x86_native_name_at(a);
        }
        fprintf(stderr, "[TRACE]   %-22s esp %08x -> %08x  (%+d)  ",
                g_ring[k].what, g_ring[k].esp_in, g_ring[k].esp_out,
                (int)(g_ring[k].esp_out - g_ring[k].esp_in));
        if (m)
            fprintf(stderr, "%s!0x%08x %s", m->name, guest, nm ? nm : "(unnamed)");
        else if (b)
            fprintf(stderr, "0x%08x (linked ep; no module has base 0x%08x)", a, b);
        else
            fprintf(stderr, "0x%08x (no registered module)", a);
        if (g_ring[k].ret) {
            /* The caller, by return address. Its enclosing function is not
               resolved here: only entry points are named, and a return address
               is by definition in the middle of one. The raw address is what a
               disassembly listing is indexed by, so it is what gets printed. */
            uint32_t r = g_ring[k].ret;
            X86Module *rm = x86_module_for(r);
            fprintf(stderr, "  <- 0x%08x", rm ? rm->preferred + (r - *rm->base) : r);
            if (rm) fprintf(stderr, " in %s", rm->name);
        }
        if (g_ring[k].repeat) fprintf(stderr, "  x%u identical", g_ring[k].repeat + 1);
        fputc('\n', stderr);
    }
}

/* ---- the abort paths ---------------------------------------------------
 *
 * Each of these names what is missing and stops. None of them may return a
 * plausible value: the native build has no original image mapped alongside it
 * and no Windows loader resolved anything, so there is nothing honest to fall
 * back TO. A recompilation that quietly ran something else would not be one.
 */
/*
 * An address may be a poisoned import slot rather than code. The host owns
 * that table, so it supplies this; the weak default keeps the runtime usable
 * on its own. Without it, a poisoned slot reached by DISPATCH (a `call [iat]`
 * the recompiler turned into an indirect call rather than a named stub) is
 * reported as "no registered module", which reads as a linking problem rather
 * than as the unimplemented import it is -- measured, on the exe's first run.
 */
__attribute__((weak))
const char *x86_poison_name(uint32_t addr, const char **mod)
{
    (void)addr; (void)mod;
    return NULL;
}

static void where(uint32_t addr)
{
    X86Module *m = x86_module_for(addr);
    const char *mod = NULL, *sym = x86_poison_name(addr, &mod);
    if (sym) {
        fprintf(stderr, "  that address is an UNBOUND IMPORT: %s!%s\n"
                        "  it was reached as a call target, so something took "
                        "its address from the IAT\n", mod, sym);
        return;
    }
    if (m)
        fprintf(stderr, "  mapped 0x%08x is in %s (guest 0x%08x)\n",
                addr, m->name, m->preferred + (addr - *m->base));
    else
        fprintf(stderr, "  address is in NO registered module -- either it is "
                        "host memory or a module was never linked in\n");
}

static void x86_dispatch_one(CPU *C, uint32_t target)
{
    X86Module *m;
    if (x86_native_call_at(target, C)) return;
    fprintf(stderr, "x86_dispatch: no recompiled body at 0x%08x\n", target);
    where(target);
    /*
     * WHO dispatched there. Every emitted indirect call pushes its own return
     * address before dispatching, so the word at ESP names the call site --
     * and without it this report says only that a bad target was reached,
     * which is the one thing the reader already knows. The value is checked
     * against the module list rather than trusted: if the stack is the thing
     * that is wrong, the return address is wrong too, and saying so is part of
     * the answer.
     */
    {
        uint32_t ra = RD32(C->esp);
        const char *nm = x86_native_name_at(ra);
        X86Module *rm = x86_module_for(ra);
        if (nm)
            fprintf(stderr, "  dispatched from 0x%08x -- %s\n", ra, nm);
        else if (rm)
            fprintf(stderr, "  dispatched from 0x%08x, inside %s (guest "
                            "0x%08x) but not at a body this host can name\n",
                    ra, rm->name, rm->preferred + (ra - *rm->base));
        else
            fprintf(stderr, "  the return address on the guest stack is "
                            "0x%08x, which is in no module either -- so the "
                            "STACK is suspect, not just the target\n", ra);
    }
    /* If it is inside a module, it is a function static analysis missed --
       exactly what the constructor-table report describes, so it is printed in
       the SAME shape and tools/native_discover.sh seeds it without needing to
       know that an indirect call target is a different kind of gap. */
    x86_diag_dump();
    m = x86_module_for(target);
    if (m) {
        fprintf(stderr, "\n*** dispatch target with no recompiled body.\n"
                        "    Reached as an indirect call, so nothing in the "
                        "database references it as code.\n");
        fprintf(stderr, "    %-18s 0x%08x\n", m->name,
                m->preferred + (target - *m->base));
        fprintf(stderr, "*** 1 of 1 dispatch target is missing a body\n");
    }
    abort();
}

void x86_dispatch(CPU *C, uint32_t target)
{
    uint32_t outer_depth = C->dispatch_depth;
    uint32_t outer_target = C->tail_target;
    C->dispatch_depth = C->call_depth + 1u;
    do {
        C->tail_target = 0;
        x86_dispatch_one(C, target);
        target = C->tail_target;
    } while (target);
    C->dispatch_depth = outer_depth;
    C->tail_target = outer_target;
}

void x86_tail_dispatch(CPU *C, uint32_t target)
{
    /*
     * Same generated-body contract as the hosted runtime (C181). A tail jump
     * reached through a direct C call must finish before that direct caller
     * resumes; only a tail at THIS dispatch frame may be queued for the loop.
     *
     * The one-level depth relation is the whole test. X86_TAIL_FN has already
     * decremented call_depth by the time this runs -- the body is leaving --
     * so the body that IS the dispatch frame arrives at dispatch_depth - 1,
     * and a body one direct call deeper arrives at exactly dispatch_depth.
     * Comparing the depths for equality therefore queued precisely the case
     * that must run inline: exactly backwards.
     *
     * What that cost: FUN_0046b750 -> FUN_00427c30 -> FUN_00426330, then a
     * tail jump to __security_check_cookie. The cookie check was queued rather
     * than run, so the return address it should have popped stayed on the
     * stack, FUN_0046b750 resumed four bytes low, and its own /GS epilogue
     * read the word below its cookie and reported a stack buffer overrun that
     * had never happened (issue #81, C213).
     */
    if (x86_tail_route(C->dispatch_depth, C->call_depth) == X86_TAIL_QUEUE) {
        C->tail_target = target;
        return;
    }
    x86_dispatch(C, target);
}

/*
 * How often this fires is itself a measurement.
 *
 * A correct translation should reach it almost never: it means a function's
 * RET popped something OTHER than the value that was on the stack when it was
 * entered, i.e. the guest deliberately rewrote its own return address. If it
 * is firing in a loop, the interesting question is not where control went but
 * why the epilogue disagreed with the prologue.
 */
unsigned long x86_return_to_count(void) { return g_return_to_calls; }

void x86_return_to(CPU *C, uint32_t target, uint32_t fn_ep, uint32_t expected)
{
    const char *nm;
    g_return_to_calls++;
    ring_note("RET-to", target, 0, C->esp, C->esp, 0);
    if (x86_native_call_at(target, C)) return;
    nm = x86_native_name_at(fn_ep);
    /*
     * A RET whose popped value is INSIDE a mapped module is an ordinary
     * return, not corruption.
     *
     * This used to abort, and it was wrong for every TAIL-CALLED body. A
     * function reached by a tail JMP is entered with whatever the jumping
     * function left at [esp] -- not its own return address -- so `_rt !=
     * _retaddr` is true by construction, and the value popped is the
     * legitimate return address of the function that jumped. Returning is
     * correct: the emitted tail call is `call; return;`, so the host call
     * chain mirrors the guest one and the guest ESP is already right.
     *
     * XMen2.exe 0x005fac10 tail-jumps into 0x005fafc1, whose RET pops
     * 0x005fb2bc -- a perfectly good return address, mid-function as every
     * return address is. Aborting on it killed a run that was fine, and sent
     * two sessions into re-splitting and re-merging the containing function
     * (issue #27).
     *
     * A value in NO module is still fatal: that is real corruption, and it is
     * the case this check was built for.
     */
    if (x86_module_for(target)) {
        static int said;
        if (!said++)
            fprintf(stderr,
                    "x86_return_to: 0x%08x (in %s) was popped by the RET in "
                    "0x%08x (%s), which\n"
                    "  had 0x%08x at [esp] on entry. That is what a TAIL CALL "
                    "looks like -- the body was jumped\n"
                    "  into, so its entry [esp] is not its own return address "
                    "-- and returning is correct.\n"
                    "  Reported once; the total is in the RET-to counter "
                    "above.\n",
                    target, x86_module_for(target)->name, fn_ep,
                    nm ? nm : "?", expected);
        return;
    }
    fprintf(stderr, "x86_return_to: 0x%08x is not a function entry, and is in "
                    "NO mapped module -- a RET popped something that cannot be "
                    "a return address.\n"
                    "  The RET is in 0x%08x (%s), which was ENTERED with "
                    "0x%08x on the stack and left with 0x%08x there.\n"
                    "  So that function's epilogue does not match its "
                    "prologue: its detected boundaries are wrong, or an\n"
                    "  instruction in it was mistranslated.\n",
                    target, fn_ep, nm ? nm : "?", expected, target);
    where(target);
    x86_diag_dump();
    abort();
}

void x86_call_unknown(CPU *C, uint32_t target)
{
    X86Module *m;
    (void)C;
    fprintf(stderr, "x86_call_unknown: 0x%08x has no identified function\n",
            target);
    where(target);
    /* Report it in the SAME shape as a missing dispatch target and a missing
       constructor target, because tools/native_discover.sh parses that shape
       and is otherwise blind to this one -- a direct call to an address Ghidra
       did not identify is the same kind of gap and the same kind of seed, and
       the loop having three reporters and understanding two of them is how a
       stop reads as "nothing more to discover" when it is not. */
    m = x86_module_for(target);
    if (m) {
        fprintf(stderr, "\n*** direct call to an address with no identified "
                        "function.\n");
        fprintf(stderr, "    %-18s 0x%08x\n", m->name,
                m->preferred + (target - *m->base));
        fprintf(stderr, "*** 1 of 1 call target is missing a body\n");
    }
    x86_diag_dump();
    abort();
}

void x86_missing_import(const char *mod, const char *sym)
{
    fprintf(stderr, "x86_missing_import: %s!%s is not implemented natively.\n"
                    "  This is the native import surface -- the work that "
                    "replaces Wine.\n", mod, sym);
    /*
     * WHO asked for it. The import's name says what is missing; it does not say
     * which subsystem wanted it, and that is what decides whether the answer is
     * an implementation or a different design. Every emitted call site pushes
     * its return address before the stub runs, so the word at ESP names the
     * caller -- and it is checked against the module list rather than trusted,
     * because a wrong stack makes the return address wrong too.
     *
     * Reading it took a run with a trace build and a manual grep through the
     * boundary ring, on a ring the OTHER guest threads were also writing to.
     */
    if (g_cpu_current) {
        uint32_t ra = RD32(g_cpu_current->esp);
        const char *nm = x86_native_name_at(ra);
        X86Module *rm = x86_module_for(ra);
        if (nm)
            fprintf(stderr, "  asked for by 0x%08x -- %s\n", ra, nm);
        else if (rm)
            fprintf(stderr, "  asked for by 0x%08x, inside %s (guest 0x%08x) "
                            "but not at a body this host can name\n",
                    ra, rm->name, rm->preferred + (ra - *rm->base));
        else
            fprintf(stderr, "  the word at the guest ESP is 0x%08x, which is in "
                            "no module -- so the caller cannot be named and the "
                            "STACK is suspect too\n", ra);
    } else {
        fprintf(stderr, "  no guest CPU is current, so the caller cannot be "
                        "named -- this was reached from host code, not from a "
                        "recompiled body\n");
    }
    x86_diag_dump();
    abort();
}

void x86_unsupported_insn(uint32_t ep, uint32_t addr, const char *name,
                          const char *reason)
{
    /* Guest (linked) addresses, labelled -- see x86_untranslated. */
    fprintf(stderr, "x86_unsupported_insn: reached guest 0x%08x, inside guest "
                    "0x%08x %s\n"
                    "  the translator could not handle it: %s\n"
                    "  The rest of that function IS translated; this is the one "
                    "instruction, and it was actually executed.\n",
            addr, ep, name, reason);
    x86_diag_dump();
    abort();
}

void x86_untranslated(uint32_t ep, const char *name, const char *reason)
{
    /* A GUEST (linked) address deliberately: it is what a reader pastes into
       Ghidra. It is labelled because three separate reporters have already
       named the wrong module by leaving that ambiguous (C101). */
    fprintf(stderr, "x86_untranslated: reached guest 0x%08x %s -- blocked by: "
                    "%s\n",
            ep, name, reason);
    x86_diag_dump();
    abort();
}

void x86_note_fallback(uint32_t target)
{
    fprintf(stderr, "x86_note_fallback: 0x%08x -- the native build has no "
                    "original image to fall back to\n", target);
    abort();
}

/*
 * The native build has NO hybrid fallback, and says so.
 *
 * On the Wine path a dispatched target with no recompiled body falls back to
 * the ORIGINAL machine code, which keeps the program alive and is honest only
 * because it is loud -- re-frontier carries it as `rc-hybrid`, standing debt.
 * Natively there is no such path: x86_allow_fallback is never set (only
 * src/app/x2run.c sets it, and that is the Wine front end), and
 * x86_note_fallback aborts by name rather than running anything.
 *
 * This used to be an empty function. Nothing calls it on this path, so it was
 * not lying -- but the fact it could have stated is worth stating: every
 * instruction a native run executes came from the translator. That is the
 * property the whole project is for, and it was going unreported.
 */
void x86_fallback_report(void)
{
    printf("  recomp: NO original machine code ran. The native build has no "
           "hybrid fallback -- a dispatched target with no recompiled body "
           "aborts by name (x86_note_fallback), so reaching this line at all "
           "means every instruction executed came from the translator.\n");
}

void x86_guest_addr_of(uint32_t addr, const char **mod, uint32_t *guest)
{
    X86Module *m = x86_module_for(addr);
    if (!m) { *mod = NULL; *guest = addr; return; }
    *mod = m->name;
    *guest = m->preferred + (addr - *m->base);
}

void x86_fallthrough(uint32_t fn_ep, uint32_t next)
{
    fprintf(stderr, "x86_fallthrough: the body of 0x%08x ended without a "
                    "terminator and falls through to 0x%08x, which is not a "
                    "known function.\n"
                    "  Its detected boundaries are wrong and the code it runs "
                    "into has never been translated.\n", fn_ep, next);
    where(next);
    x86_diag_dump();
    abort();
}

/*
 * The body ended at a call the ORIGINAL analyser proved never returns
 * (longjmp, exit, terminate, _CxxThrowException). Getting here is not a
 * boundary defect -- the boundaries are right -- it means OUR implementation
 * of that callee came back. Naming the callee is the whole point: the two
 * failures need completely different work, and for six of XMen2.exe's fifteen
 * "truncated" bodies the boundary story was simply wrong.
 */
void x86_after_noreturn(uint32_t fn_ep, const char *callee)
{
    fprintf(stderr, "x86_after_noreturn: the body of 0x%08x ends at a call to "
                    "%s, which NEVER RETURNS.\n"
                    "  Execution came back from it, so the defect is in this "
                    "port's %s, not in the function's boundaries.\n",
            fn_ep, callee, callee);
    x86_diag_dump();
    abort();
}

void x86_int3(uint32_t addr)
{
    fprintf(stderr, "x86_int3: execution reached the compiler's unreachable "
                    "trap at guest 0x%08x.\n"
                    "  MSVC emits INT3 after a call it proved never returns, "
                    "so a noreturn function returned.\n", addr);
    where(addr);
    x86_diag_dump();
    abort();
}

void x87_fault(const char *what)
{
    /*
     * A modelled-x87 fault used to print four words and abort, which says
     * WHICH invariant broke and nothing about where. The stack depth is a
     * property of a translated body -- an FSTP the translator emitted without
     * its matching push, or a body entered at the wrong place -- so the ring,
     * which names the last bodies entered and who called them, is exactly the
     * evidence needed and it was being thrown away.
     */
    fprintf(stderr, "x87_fault: %s\n"
                    "  This is the MODELLED x87 stack, so it is a translation "
                    "defect, not a guest bug: some body pushed or popped a "
                    "different number of times than the original.\n", what);
    /*
     * The CALLER, by host return address.
     *
     * The ring names the last bodies ENTERED, which is the neighbourhood; it
     * cannot name the instruction, and "somewhere in a 672-instruction float
     * routine" is not a place. x87_pop is inlined into the generated body, so
     * this return address is inside that body, and addr2line turns it into the
     * emitted line -- whose comment carries the guest address of the exact
     * instruction. The binary is PIE, so the load base has to come off first
     * or addr2line silently answers "??".
     */
    {
        unsigned long ra = (unsigned long)__builtin_return_address(0);
        Dl_info di;
        if (dladdr((void *)ra, &di) && di.dli_fbase)
            fprintf(stderr, "  the body that did it:  addr2line -fCe "
                            "<this binary> 0x%lx\n",
                    ra - (unsigned long)di.dli_fbase);
        else
            fprintf(stderr, "  (dladdr could not give the load base, so the "
                            "return address 0x%lx cannot be turned into a file "
                            "offset here)\n", ra);
    }
    x86_diag_dump();
    abort();
}

/*
 * Call an import through its IAT slot.
 *
 * The slot is bound at startup by the host, the way a loader would bind it, so
 * a call into another recompiled module lands on that module's body at its
 * mapped address. The module and symbol are carried along only for the failure
 * case: an unbound slot holds a poison address, and reporting "libIGCore.dll!
 * ?createInstance@..." is worth far more than reporting 0x00090120.
 */
int x86_is_thunk(uint32_t addr)
{
    return addr >= THUNK_BASE && addr < THUNK_BASE + (uint32_t)THUNK_MAX * 16u;
}

void x86_import_call(CPU *C, uint32_t slot_va, const char *mod, const char *sym)
{
    uint32_t target = *(volatile uint32_t *)(uintptr_t)slot_va;
    uint32_t esp_in = C->esp;
    /* The cycle break. Reaching here means the generated stub is running,
       which only happens when nothing implements this import natively -- so a
       slot pointing back at the thunk that calls this very stub is not a
       target, it is the loop. Report it instead of taking it. */
    if (x86_is_thunk(target)) x86_missing_import(mod, sym);
    if (x86_native_call_at(target, C)) {
        ring_note(sym, slot_va, 0, esp_in, C->esp, 0);
        return;
    }
    fprintf(stderr, "x86_import_call: %s!%s\n"
                    "  slot 0x%08x holds 0x%08x, which is not a recompiled "
                    "body.\n", mod, sym, slot_va, target);
    x86_missing_import(mod, sym);
}

/*
 * Call a guest function FROM host code.
 *
 * A recompiled body is entered with its return address already on the guest
 * stack -- every emitted call site pushes one -- and its RET pops it. Host
 * code that dispatches without pushing one therefore leaks 4 bytes of guest
 * stack per call, upward, and the damage is silent until ESP walks off the
 * top: measured as a SIGSEGV 64 bytes above the stack top after 51 static
 * constructors, which reads as stack corruption rather than a missing push.
 *
 * So the convention lives here once instead of at each call site.
 */
void x86_guest_call_args(CPU *C, uint32_t target, uint32_t callee_pop_bytes)
{
    uint32_t before = C->esp;
    uint32_t expected = before + callee_pop_bytes;
    C->esp -= 4;
    *(volatile uint32_t *)(uintptr_t)C->esp = 0xDEADBEEFu;   /* popped by RET */
    x86_dispatch(C, target);
    /*
     * The balance check. This is the ONE place host code calls guest code, so
     * it is the one place the guest stack can be checked against a value the
     * host knows independently: after popping the return address, a called
     * function must leave ESP at the entry value plus exactly the declared
     * callee-cleaned argument bytes. Any other result is an ABI mismatch; there
     * is no safe value to repair it to because either the declaration or the
     * callee is wrong.
     */
    if (C->esp != expected) {
        const char *nm = x86_native_name_at(target);
        unsigned long ra = (unsigned long)__builtin_return_address(0);
        Dl_info di;
        fprintf(stderr, "x86_guest_call: 0x%08x (%s) violated its stack "
                        "contract: ESP %08x -> %08x, expected %08x after "
                        "popping %u argument byte(s).\n",
                target, nm ? nm : "?", before, C->esp, expected,
                callee_pop_bytes);
        if (dladdr((void *)ra, &di) && di.dli_fbase)
            fprintf(stderr, "  host caller: addr2line -fCe <this binary> "
                            "0x%lx\n",
                    ra - (unsigned long)di.dli_fbase);
        else
            fprintf(stderr, "  host caller could not be resolved (return "
                            "address 0x%lx).\n", ra);
        x86_diag_dump();
        abort();
    }
}

void x86_guest_call(CPU *C, uint32_t target)
{
    x86_guest_call_args(C, target, 0u);
}
