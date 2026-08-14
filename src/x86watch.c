/*
 * Entry-point watch for the recompiled PC modules.
 *
 * The question that blocks a bisection is never "which function is implicated"
 * -- bisect_recomp.sh answers that -- it is "what did that function actually
 * see when it ran". Reading the generated C answers what it WOULD do; only a
 * running build answers what it DID.
 *
 * This is the PC-side counterpart of the Xbox build's indirect-call watch
 * (I012), and it is deliberately built to the same rule: it must be able to
 * report the NEGATIVE. A watch that only prints when it fires is
 * indistinguishable from a watch that was never wired up, and "no output"
 * then reads as "the function was not involved" when it may mean "the watch
 * never ran". So every watched entry point is reported at exit, including the
 * ones with a call count of zero, in those words.
 *
 *   X2_WATCH=0x10002c00,0x10002c70   entry points to report
 *   X2_WATCH_MEM=0x10021b80          dump this guest address each time
 *   X2_WATCH_MEM=0x007ac24c,0,0x60   or follow object -> vtable -> slot
 *   X2_WATCH_MAX=8                   per-entry-point print cap (default 8)
 *   X2_WATCH_SELFTEST=1              prove both directions before the game runs
 *
 * Addresses are GUEST addresses (preferred-base 0x10000000...), the same ones
 * the disassembly and the bisect output use. Memory reads go through
 * G_IMGBASE, so a relocated module is handled -- reading the literal address
 * would silently sample whatever else the loader put there, which is the
 * failure img_rel() exists to prevent.
 */
#include "x86rt.h"
#include "x86watch_memory.h"
#include "x86watch_stack.h"
#include "x86watch_trace.h"

#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* XMen2's largest measured discriminator set is the 547 direct callers of one
   lazy engine singleton. Keep the explicit list large enough to test the whole
   class in one run instead of drawing conclusions from a hand-picked batch. */
#define WATCH_MAX_EPS 1024

/*
 * The sink is a FILE, not stderr.
 *
 * This game is a GUI-subsystem process launched under `wine explorer
 * /desktop=`, and a GUI process has no stderr: everything written there is
 * discarded. The first version of this watch wrote to stderr and produced a
 * completely silent log on a run where it had definitely fired --
 * which reads exactly like "the watched function was never called". A control
 * run against the KNOWN-GOOD build caught it; without that control the silence
 * would have been evidence for a conclusion it could not support.
 *
 *   X2_WATCH_LOG   path for the watch log (default: x2watch.log in the cwd,
 *                  which run_shim.sh makes the run directory)
 */
static FILE *w_out;

static FILE *watch_out(void)
{
    if (!w_out) {
        const char *p = getenv("X2_WATCH_LOG");
        w_out = fopen((p && *p) ? p : "x2watch.log", "w");
        if (!w_out) w_out = stderr;    /* better than nothing, if not by much */
    }
    return w_out;
}

/* The crash reporter (src/x86fault.c) writes into the SAME log, so its block
   lands in sequence with the ENTER lines that lead up to the fault. Two files
   would lose exactly the ordering that localises the fault. */
FILE *x86_watch_log(void) { return watch_out(); }

static uint32_t      w_ep[WATCH_MAX_EPS];
static unsigned long w_hits[WATCH_MAX_EPS];
static int           w_n = -1;          /* -1 = not yet parsed */
static X86MemWatch   w_mem;
static int           w_mem_enabled;
static uint32_t      w_mem_last;
static uint32_t      w_mem_address_last;
static int           w_mem_resolved_last;
static int           w_mem_seen;
static int           w_cap = 8;
/*
 * X2_WATCH=all: report EVERY recompiled entry point, capped globally.
 *
 * Needed because "watch these specific addresses" cannot supply its own
 * positive control. On the known-good build, watching six entry points from
 * the verified set produced no output at all -- which could mean the hook is
 * broken, or that none of those six is called during the intro. Only a trace
 * of everything distinguishes those, and it is also what shows which function
 * ran last before a crash.
 */
static int           w_all;
static unsigned long w_all_seen;

static void x86_watch_report(void);

static void watch_parse(void)
{
    const char *s = getenv("X2_WATCH");
    const char *m = getenv("X2_WATCH_MEM");
    const char *c = getenv("X2_WATCH_MAX");
    w_n = 0;
    if (c && *c) w_cap = atoi(c);
    if (m && *m) {
        char error[128];
        w_mem_enabled = x86_memwatch_parse(&w_mem, m, error, sizeof error);
        if (!w_mem_enabled)
            fprintf(watch_out(), "[MEM] REFUSING X2_WATCH_MEM=\"%s\": %s\n",
                    m, error);
    }
    if (!s || !*s) return;
    if (s[0] == 'a' && s[1] == 'l' && s[2] == 'l' && s[3] == '\0') {
        w_all = 1;
        fprintf(watch_out(), "[WATCH] watching ALL entry points, global cap %d\n",
                w_cap);
        atexit(x86_watch_report);
        return;
    }
    while (*s && w_n < WATCH_MAX_EPS) {
        char *end;
        unsigned long v = strtoul(s, &end, 0);
        if (end == s) break;                    /* not a number: stop, loudly */
        w_ep[w_n++] = (uint32_t)v;
        s = end;
        while (*s == ',' || *s == ' ') s++;
    }
    /* A typo in the list must not silently watch nothing. */
    if (*s)
        fprintf(watch_out(), "[WATCH] X2_WATCH has trailing junk that parsed to no "
                        "address: \"%s\" -- those entry points are NOT watched\n", s);
    fprintf(watch_out(), "[WATCH] watching %d entry point(s), cap %d per point%s\n",
            w_n, w_cap, w_mem_enabled ? ", with a memory watch" : "");
    atexit(x86_watch_report);
}

static int watch_slot(uint32_t ep)
{
    int i;
    if (w_n < 0) watch_parse();
    for (i = 0; i < w_n; i++)
        if (w_ep[i] == ep) return i;
    return -1;
}

static uint32_t watch_read32(void *context, uint32_t address)
{
    (void)context;
    return RD32(address);
}

static int watch_mem_change(uint32_t ep, uint32_t *address,
                            uint32_t *value, int *resolved)
{
    uint32_t host;
    if (!w_mem_enabled)
        return 0;
    host = G_IMGBASE + (w_mem.root - g_guest_preferred_base);
    *resolved = x86_memwatch_read(&w_mem, host, watch_read32, NULL,
                                  address, value);
    if (w_mem_seen && *resolved == w_mem_resolved_last &&
        *address == w_mem_address_last && *value == w_mem_last)
        return 0;
    w_mem_seen = 1;
    w_mem_resolved_last = *resolved;
    w_mem_address_last = *address;
    w_mem_last = *value;
    if (*resolved)
        fprintf(watch_out(), "[MEM] before 0x%08x: chain 0x%08x resolved "
                "[0x%08x]=0x%08x CHANGED\n",
                ep, w_mem.root, *address, *value);
    else
        fprintf(watch_out(), "[MEM] before 0x%08x: chain 0x%08x UNRESOLVED "
                "(a required pointer is NULL) CHANGED\n", ep, w_mem.root);
    fflush(watch_out());
    return 1;
}

/*
 * Entry AND exit, because "it was called" and "it came back" are different
 * facts and only the pair localises a fault. A watch that prints only on entry
 * cannot distinguish "crashed inside the callee" from "returned something that
 * killed the caller later" -- which is exactly the question a page fault whose
 * EIP equals one of the arguments raises.
 */
void x86_watch_exit(uint32_t ep, const CPU *C)
{
    int i;
    if (w_n < 0) watch_parse();
    x86_watch_note(3, ep, C->esp);
    if (w_all) {
        if (w_all_seen > (unsigned long)w_cap) return;
        fprintf(watch_out(), "[WATCH] 0x%08x RETURNED  eax=0x%08x esp=0x%08x\n",
                ep, C->eax, C->esp);
        fflush(watch_out());
        return;
    }
    i = watch_slot(ep);
    if (i < 0 || (int)w_hits[i] > w_cap) return;
    fprintf(watch_out(), "[WATCH] 0x%08x #%lu RETURNED  eax=0x%08x esp=0x%08x\n",
            ep, w_hits[i], C->eax, C->esp);
    fflush(watch_out());
}

void x86_watch_enter(uint32_t ep, const CPU *C)
{
    int i;
    uint32_t mem_address = 0;
    uint32_t mem_value = 0;
    int mem_changed = 0;
    int mem_resolved = 0;
    if (w_n < 0) watch_parse();
    x86_watch_note(0, ep, C->esp);
    mem_changed = watch_mem_change(ep, &mem_address, &mem_value, &mem_resolved);
    if (w_all) {
        if (++w_all_seen > (unsigned long)w_cap) return;
        fprintf(watch_out(), "[WATCH] 0x%08x ENTER  esp=0x%08x ecx=0x%08x "
                             "ret=0x%08x arg0=0x%08x\n",
                ep, C->esp, C->ecx,
                C->esp ? RD32(C->esp) : 0u, C->esp ? RD32(C->esp + 4) : 0u);
        fflush(watch_out());
        return;
    }
    i = watch_slot(ep);
    if (i < 0) return;
    w_hits[i]++;
    /* Cap identical repeats, never state changes. Otherwise the diagnostic
       stops looking before the event it was created to find. */
    if ((int)w_hits[i] > w_cap && !mem_changed) return;
    fprintf(watch_out(), "[WATCH] 0x%08x #%lu  esp=0x%08x ecx=0x%08x "
                    "ret=0x%08x arg0=0x%08x",
            ep, w_hits[i], C->esp, C->ecx,
            C->esp ? RD32(C->esp) : 0u,
            C->esp ? RD32(C->esp + 4) : 0u);
    if (w_mem_enabled) {
        if (mem_resolved)
            fprintf(watch_out(), "  chain[0x%08x -> 0x%08x]=0x%08x%s",
                    w_mem.root, mem_address, mem_value,
                    mem_changed ? " CHANGED" : "");
        else
            fprintf(watch_out(), "  chain[0x%08x]=UNRESOLVED%s", w_mem.root,
                    mem_changed ? " CHANGED" : "");
    }
    fprintf(watch_out(), "\n");
    fflush(watch_out());
}

void x86_watch_stack(uint32_t ep, uint32_t guest_esp, const void *cpu,
                     unsigned long cpu_size)
{
    static int done;
    if (done) return;
    done = 1;
    if (w_n < 0) watch_parse();
    x86_watch_stack_report(watch_out(), ep, guest_esp,
                           (uint32_t)(uintptr_t)cpu, cpu_size);
    fflush(watch_out());
}

/*
 * A ring of the last few control transfers across the recompiled/host boundary.
 *
 * A snapshot at the fault says where execution ended up; it does not say how it
 * got there, and on this boundary the interesting transfers happen in HOST code
 * that no watch can see from the inside. Three faults in a row were read by
 * hand-simulating the stack from one register dump, and the reading was wrong
 * twice. The sequence is cheap to record and is what the fault report prints.
 *
 * Kinds: 0 = entered a recompiled body, 3 = it returned, 1 = called a host
 * function, 2 = that host function returned.
 */
void x86_watch_note(int kind, uint32_t a, uint32_t b)
{
    x86_watch_trace_note(kind, a, b, (unsigned long)GetCurrentThreadId());
}

void x86_watch_note_dump(FILE *o)
{
    x86_watch_trace_dump(o);
}

static void x86_watch_report(void)
{
    int i;
    if (w_all) {
        fprintf(watch_out(), "[WATCH] %lu recompiled entry point call(s) total"
                             " (printed the first %d)\n", w_all_seen, w_cap);
        fflush(watch_out());
        return;
    }
    if (w_n <= 0) return;
    fprintf(watch_out(), "[WATCH] final tally:\n");
    for (i = 0; i < w_n; i++) {
        if (w_hits[i])
            fprintf(watch_out(), "  0x%08x  %lu call(s)\n", w_ep[i], w_hits[i]);
        else
            fprintf(watch_out(), "  0x%08x  NEVER CALLED -- this run says nothing "
                            "about it\n", w_ep[i]);
    }
    fflush(watch_out());
}

/*
 * Prove the watch fires AND that it can report a miss, before the game runs.
 *
 * Only the positive half is tempting to write, and it is the half that cannot
 * fail usefully: a watch hard-wired to print on every entry point would pass
 * it. The negative half -- an address that is never entered must come back as
 * NEVER CALLED -- is what makes a silent log trustworthy.
 */
void x86_watch_selftest(void)
{
    CPU fake;
    uint32_t ep_hit, ep_miss;
    int i_hit, i_miss;
    const char *e = getenv("X2_WATCH_SELFTEST");
    if (!e || e[0] != '1') return;
    if (w_n < 0) watch_parse();
    if (w_all) {
        /* Say so rather than returning quietly: a self-test that silently
           declines in one mode is the same lie it exists to prevent. */
        fprintf(watch_out(), "[WATCH] SELFTEST: X2_WATCH=all has no per-address"
                        " table to check, so only the SINK is proven here --"
                        " this line reaching the log IS that proof. Zero ENTER"
                        " lines after it therefore means zero entries, not a"
                        " dead instrument.\n");
        fflush(watch_out());
        return;
    }
    if (w_n < 1) {
        fprintf(watch_out(), "[WATCH] SELFTEST cannot run: X2_WATCH is empty, so "
                        "there is nothing to prove\n");
        return;
    }
    ep_hit = w_ep[0];
    /* An address no module claims, so it can never be entered for real. */
    ep_miss = 0xDEAD0000u;
    if (w_n < WATCH_MAX_EPS) w_ep[w_n++] = ep_miss;

    i_hit = watch_slot(ep_hit);
    i_miss = watch_slot(ep_miss);
    memset(&fake, 0, sizeof fake);
    fake.esp = 0;                      /* 0 esp: the printer must not deref it */
    x86_watch_enter(ep_hit, &fake);

    if (i_hit < 0 || w_hits[i_hit] == 0) {
        fprintf(watch_out(), "[WATCH] SELFTEST FAILED: a watched entry point did "
                        "not register a call. Every [WATCH] line this run is "
                        "unreliable.\n");
    } else if (i_miss < 0 || w_hits[i_miss] != 0) {
        fprintf(watch_out(), "[WATCH] SELFTEST FAILED: an entry point that was "
                        "never entered reports %lu call(s), so NEVER CALLED "
                        "means nothing.\n",
                i_miss < 0 ? 0ul : w_hits[i_miss]);
    } else {
        fprintf(watch_out(), "[WATCH] SELFTEST passed: a watched entry point counts "
                        "a call, and one that is never entered stays at zero.\n");
    }
    /* Roll the synthetic call back so the real tally stays honest. */
    if (i_hit >= 0 && w_hits[i_hit]) w_hits[i_hit]--;
    fflush(watch_out());
}
