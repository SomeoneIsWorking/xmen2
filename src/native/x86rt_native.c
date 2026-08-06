/*
 * The shared native runtime: dispatch across every linked recompiled module.
 * See x86rt_native.h for why dispatch keys on the mapped address rather than
 * the guest entry point.
 */
#include "x86rt.h"
#include "x86rt_native.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/uio.h>

static X86Module *g_head;

static int thunk_call(uint32_t addr, CPU *C);
static void ring_note(const char *what, uint32_t addr, uint32_t base,
                      uint32_t in, uint32_t out);
static unsigned long g_return_to_calls;
extern const CPU *g_cpu_current;

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

/* Linear search per module. 5769 + 521 entries is small enough that this has
   never shown up in a profile, and a sorted table would have to be built at
   registration -- worth doing when it measurably matters, not before. */
static const X86Fn *find(X86Module *m, uint32_t addr)
{
    uint32_t ep = m->preferred + (addr - *m->base);
    int i;
    for (i = 0; i < m->nfns; i++)
        if (m->fns[i].ep == ep) return &m->fns[i];
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
    m = x86_module_for(addr);
    const X86Fn *f = m ? find(m, addr) : NULL;
    if (!f) return 0;
    {
        uint32_t in = C->esp;
        g_cpu_current = C;
        f->fn(C);
        ring_note("guest", addr, 0, in, C->esp);
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
    {
        void *save = g_cb_ctx;
        g_cb_ctx = g_thunk[i].ctx;
        g_thunk[i].stub(C);
        g_cb_ctx = save;
    }
    ring_note(g_thunk[i].sym, addr, 0, in, C->esp);
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
    unsigned    repeat;
} g_ring[RING];
static unsigned g_ring_n;

/*
 * Consecutive identical crossings collapse into one entry with a count.
 *
 * Without it a hot leaf drowns the ring: one four-instruction index helper
 * called in a loop filled all 96 slots with the same line, and the history
 * that mattered -- what happened BEFORE the imbalance -- had already scrolled
 * out. Capping the boring case rather than the interesting one is the whole
 * point of a ring this size.
 */
static void ring_note(const char *what, uint32_t addr, uint32_t base,
                      uint32_t in, uint32_t out)
{
    unsigned i;
    if (g_ring_n) {
        i = (g_ring_n - 1) % RING;
        if (g_ring[i].addr == addr && g_ring[i].base == base
            && g_ring[i].esp_in == in && g_ring[i].esp_out == out) {
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
    g_ring[i].repeat = 0;
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
#define ARGS_MAX 16
static uint32_t g_args_ep[ARGS_MAX];
static int g_args_n = -1, g_args_hits;

static void args_init(void)
{
    const char *e = getenv("X2_ARGS");
    char buf[256], *p, *save;
    g_args_n = 0;
    if (!e || !*e) return;
    snprintf(buf, sizeof buf, "%s", e);
    for (p = strtok_r(buf, ",", &save); p && g_args_n < ARGS_MAX;
         p = strtok_r(NULL, ",", &save))
        g_args_ep[g_args_n++] = (uint32_t)strtoul(p, NULL, 0);
    fprintf(stderr, "[ARGS] watching %d entry point(s); ECX and 4 stack words "
                    "per call. The real argument count is unknown, so trailing "
                    "words may be the caller's frame rather than arguments.\n",
            g_args_n);
}

static int args_watched(uint32_t ep)
{
    int i;
    if (g_args_n < 0) args_init();
    for (i = 0; i < g_args_n; i++) if (g_args_ep[i] == ep) return 1;
    return 0;
}

/* Reported at exit so a watch that never fired cannot be read as "it was
   called with nothing interesting". */
void x86_args_report(void)
{
    if (g_args_n < 0) args_init();
    if (!g_args_n) return;
    if (!g_args_hits)
        fprintf(stderr, "[ARGS] NONE of the %d watched entry point(s) was "
                        "entered -- this run says nothing about their "
                        "arguments.\n", g_args_n);
    else
        fprintf(stderr, "[ARGS] %d call(s) reported across %d watched entry "
                        "point(s).\n", g_args_hits, g_args_n);
}

void x86_trace_enter(uint32_t ep, uint32_t base, const CPU *C)
{
    ring_note("enter", ep, base, C->esp, C->esp);
    if (args_watched(ep)) {
        /* Resolve through the module that HAS this base, never by assuming a
           preferred address: the exe is linked for 0x400000, not 0x10000000,
           and hardcoding one is how a report names the wrong function. */
        X86Module *m;
        const char *nm = NULL;
        for (m = x86_modules(); m; m = m->next)
            if (*m->base == base) { nm = x86_native_name_at(base + (ep - m->preferred)); break; }
        g_args_hits++;
        fprintf(stderr, "[ARGS] -> 0x%08x %-38s ecx %08x  args %08x %08x "
                        "%08x %08x  (ret to %08x)\n",
                ep, nm ? nm : "", C->ecx,
                RD32(C->esp + 4), RD32(C->esp + 8),
                RD32(C->esp + 12), RD32(C->esp + 16), RD32(C->esp));
        /* X2_PEEK at every watched call, not only at the fault. A dump taken
           once at the end shows the wreckage; what identifies WHICH call broke
           an invariant is the same addresses before and after each one. */
        x86_peek_report();
    }
}

void x86_trace_exit(uint32_t ep, uint32_t base, const CPU *C)
{
    ring_note("exit", ep, base, C->esp, C->esp);
    if (args_watched(ep)) {
        fprintf(stderr, "[ARGS] <- 0x%08x  eax %08x\n", ep, C->eax);
        x86_peek_report();
    }
}
#endif

#ifdef X86_NATIVE_REACHED
/* ---- the reached set --------------------------------------------------
 *
 * Open-addressed, power-of-two, never resized: 36,340 functions across the
 * eight modules, so 131,072 slots keeps the load factor under 0.28 and the
 * probe count near one. Never resized means never allocating from a hook that
 * runs inside guest execution.
 *
 * The key is the entry point as LINKED, which is what the generated bodies
 * carry. Every libIG*.dll is linked for 0x10000000, so two modules CAN have a
 * function at the same linked address -- the report says so rather than
 * quietly reporting a hit in one module as a hit in another.
 */
#define REACHED_SLOTS (1u << 17)
/* seq is the ORDER of first entry, 1-based. Reached-or-not alone cannot answer
   "did the exe set the flag before the engine read it?", which is the question
   an ordering bug always reduces to, and a ring cannot answer it either
   because it evicts. One counter turns the set into a first-touch ordering at
   no extra cost. */
/* n is the CALL COUNT, not just presence. "Reached" and "reached 31 times" are
   different findings: a loop that fails to advance re-runs the same body, and
   presence alone reports that as indistinguishable from running it once.
   The key is (ep, base) -- see x86rt.h for why the ep alone is not unique. */
static struct { uint32_t ep, base, seq, n; } g_reached[REACHED_SLOTS];
static unsigned g_reached_n;

static unsigned reached_slot(uint32_t ep, uint32_t base)
{
    uint32_t h = (ep * 2654435761u + base * 40503u) >> 11;
    for (;;) {
        unsigned i = h & (REACHED_SLOTS - 1);
        if (g_reached[i].ep == 0) return i;
        if (g_reached[i].ep == ep && g_reached[i].base == base) return i;
        h++;
    }
}

void x86_reached_enter(uint32_t ep, uint32_t base)
{
    unsigned i = reached_slot(ep, base);
    if (g_reached[i].ep) { g_reached[i].n++; return; }
    g_reached[i].ep = ep;
    g_reached[i].base = base;
    g_reached[i].seq = ++g_reached_n;
    g_reached[i].n = 1;
}

/* 1-based order of first entry, or 0 for never entered. */
static uint32_t reached_seq(uint32_t ep, uint32_t base)
{
    unsigned i = reached_slot(ep, base);
    return g_reached[i].ep ? g_reached[i].seq : 0;
}

static uint32_t reached_count(uint32_t ep, uint32_t base)
{
    unsigned i = reached_slot(ep, base);
    return g_reached[i].ep ? g_reached[i].n : 0;
}

/*
 * Both classes, every run. A discriminator that has only been seen to say
 * "reached" has not been shown to be capable of saying "never", and this one
 * exists precisely to be believed when it says NEVER.
 */
static void reached_selftest(void)
{
    const uint32_t a = 0xDEAD0001u, b = 0xDEAD0002u, miss = 0xDEAD0003u;
    const uint32_t B1 = 0x10000000u, B2 = 0x20000000u;
    int ok_pos, ok_neg, ok_ord, ok_cnt, ok_mod;
    x86_reached_enter(a, B1);
    x86_reached_enter(b, B1);
    ok_pos = reached_seq(a, B1) != 0;
    ok_neg = reached_seq(miss, B1) == 0;
    /* Ordering is a claim this instrument makes, so it is tested too: a seq
       that is merely non-zero would pass the checks above while ordering
       everything wrongly. */
    ok_ord = reached_seq(a, B1) < reached_seq(b, B1);
    x86_reached_enter(a, B1);                  /* a second time: count must move */
    ok_cnt = reached_count(a, B1) == 2 && reached_count(b, B1) == 1;
    /* And the whole point of the (ep, base) key: the SAME ep in a different
       module must be a different entry, not the same counter. */
    x86_reached_enter(a, B2);
    ok_mod = reached_count(a, B1) == 2 && reached_count(a, B2) == 1;
    fprintf(stderr, "[REACHED] selftest: inserted -> %s; never-inserted -> %s; "
                    "order %u<%u -> %s; counts 2/1 -> %s; same ep in two "
                    "modules kept apart -> %s\n",
            ok_pos ? "REACHED (correct)" : "NEVER (WRONG)",
            ok_neg ? "NEVER (correct)" : "REACHED (WRONG)",
            reached_seq(a, B1), reached_seq(b, B1), ok_ord ? "correct" : "WRONG",
            ok_cnt ? "correct" : "WRONG", ok_mod ? "correct" : "WRONG");
    if (!ok_pos || !ok_neg || !ok_ord || !ok_cnt || !ok_mod) {
        fprintf(stderr, "[REACHED] the reached set is BROKEN in at least one "
                        "direction -- every answer below is worthless.\n");
        _exit(4);
    }
}

void x86_reached_report(void)
{
    const char *want = getenv("X2_REACHED");
    char buf[1024], *p, *save;
    if (getenv("X2_REACHED_SELFTEST")) reached_selftest();
    fprintf(stderr, "[REACHED] %u distinct (entry point, module) pairs were "
                    "entered.\n", g_reached_n);
    if (!g_reached_n)
        fprintf(stderr, "[REACHED] That is ZERO, so no body ran at all and a "
                        "NEVER below says nothing about the guest.\n");
    if (!want || !*want) {
        fprintf(stderr, "[REACHED] X2_REACHED is unset, so no specific address "
                        "was asked about. Set it to a comma-separated list of "
                        "0x... to get a verdict per address.\n");
        return;
    }
    fprintf(stderr, "[REACHED] '#n' is the ORDER of first entry (smaller ran "
                    "first); 'xN' is how many times it was entered. One line "
                    "per module defining that address.\n");
    snprintf(buf, sizeof buf, "%s", want);
    for (p = strtok_r(buf, ",", &save); p; p = strtok_r(NULL, ",", &save)) {
        uint32_t ep = (uint32_t)strtoul(p, NULL, 0);
        X86Module *m;
        int nmod = 0;
        for (m = x86_modules(); m; m = m->next) {
            uint32_t seq;
            /* Only modules whose LINKED range contains this address. Without
               the range test the subtraction wraps and probes a random mapped
               address in every other module. */
            if (ep < m->preferred || ep - m->preferred >= m->size) continue;
            if (!x86_native_name_at(*m->base + (ep - m->preferred))) continue;
            nmod++;
            seq = reached_seq(ep, *m->base);
            if (seq)
                fprintf(stderr, "[REACHED]   0x%08x  REACHED  #%-6u x%-6u %s\n",
                        ep, seq, reached_count(ep, *m->base), m->name);
            else
                fprintf(stderr, "[REACHED]   0x%08x  NEVER            %-7s %s\n",
                        ep, "", m->name);
        }
        if (!nmod)
            fprintf(stderr, "[REACHED]   0x%08x  -- NO registered module "
                            "defines a function at that address, so there is "
                            "nothing this could have counted\n", ep);
    }
}

#endif /* X86_NATIVE_REACHED */

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
    x86_peek_report();
#ifdef X86_NATIVE_REACHED
    x86_reached_report();
#endif
#ifdef X86_NATIVE_TRACE
    x86_args_report();
#endif
    x86_ring_dump();
}

void x86_ring_dump(void)
{
    unsigned n = g_ring_n < RING ? g_ring_n : RING, i;
    if (!g_ring_n) {
        fprintf(stderr, "[TRACE] the boundary ring is EMPTY: nothing crossed "
                        "between guest and host before this point.\n");
        return;
    }
    fprintf(stderr, "[TRACE] last %u of %u crossings (esp in -> out; a delta "
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

void x86_dispatch(CPU *C, uint32_t target)
{
    X86Module *m;
    if (x86_native_call_at(target, C)) return;
    fprintf(stderr, "x86_dispatch: no recompiled body at 0x%08x\n", target);
    where(target);
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
    ring_note("RET-to", target, 0, C->esp, C->esp);
    if (x86_native_call_at(target, C)) return;
    nm = x86_native_name_at(fn_ep);
    fprintf(stderr, "x86_return_to: 0x%08x is not a function entry -- a RET "
                    "popped something that is not a return address.\n"
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

void x86_fallback_report(void) { }

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
    fprintf(stderr, "x87_fault: %s\n", what);
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
        ring_note(sym, slot_va, 0, esp_in, C->esp);
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
void x86_guest_call(CPU *C, uint32_t target)
{
    uint32_t before = C->esp;
    C->esp -= 4;
    *(volatile uint32_t *)(uintptr_t)C->esp = 0xDEADBEEFu;   /* popped by RET */
    x86_dispatch(C, target);
    /*
     * The balance check. This is the ONE place host code calls guest code, so
     * it is the one place the guest stack can be checked against a value the
     * host knows independently: a called function must leave ESP exactly where
     * it was, having popped only the return address pushed above.
     *
     * A `ret N` leaves it HIGHER, which is legitimate for a stdcall body whose
     * arguments the host did not push -- so that is reported once and allowed.
     * LOWER is never legitimate: the body consumed stack that was not its own,
     * and the caller's frame is now wrong. Left unchecked it drifts, and the
     * failure lands many calls later at a RET that pops a frame pointer.
     */
    if (C->esp != before) {
        static int said;
        if ((int32_t)(C->esp - before) < 0) {
            fprintf(stderr, "x86_guest_call: 0x%08x left ESP %d bytes LOWER "
                            "than it started (%08x -> %08x). It consumed stack "
                            "that was not its own.\n",
                    target, (int)(before - C->esp), before, C->esp);
            x86_diag_dump();
            abort();
        }
        if (!said++) {
            const char *nm = x86_native_name_at(target);
            fprintf(stderr, "x86_guest_call: 0x%08x (%s) returned with ESP %d "
                            "bytes higher -- a `ret N` whose N arguments the "
                            "host never pushed. Reported once.\n",
                    target, nm ? nm : "?", (int)(C->esp - before));
        }
        C->esp = before;
    }
}
