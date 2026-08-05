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
 *   X2_WATCH_MEM=0x10021b80          also dump this guest address each time
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WATCH_MAX_EPS 32

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

static uint32_t      w_ep[WATCH_MAX_EPS];
static unsigned long w_hits[WATCH_MAX_EPS];
static int           w_n = -1;          /* -1 = not yet parsed */
static uint32_t      w_mem;             /* guest address, 0 = none */
static int           w_cap = 8;

static void x86_watch_report(void);

static void watch_parse(void)
{
    const char *s = getenv("X2_WATCH");
    const char *m = getenv("X2_WATCH_MEM");
    const char *c = getenv("X2_WATCH_MAX");
    w_n = 0;
    if (c && *c) w_cap = atoi(c);
    if (m && *m) w_mem = (uint32_t)strtoul(m, NULL, 0);
    if (!s || !*s) return;
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
            w_n, w_cap, w_mem ? ", with a memory watch" : "");
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

/*
 * Entry AND exit, because "it was called" and "it came back" are different
 * facts and only the pair localises a fault. A watch that prints only on entry
 * cannot distinguish "crashed inside the callee" from "returned something that
 * killed the caller later" -- which is exactly the question a page fault whose
 * EIP equals one of the arguments raises.
 */
void x86_watch_exit(uint32_t ep, const CPU *C)
{
    int i = watch_slot(ep);
    if (i < 0 || (int)w_hits[i] > w_cap) return;
    fprintf(watch_out(), "[WATCH] 0x%08x #%lu RETURNED  eax=0x%08x esp=0x%08x\n",
            ep, w_hits[i], C->eax, C->esp);
    fflush(watch_out());
}

void x86_watch_enter(uint32_t ep, const CPU *C)
{
    int i = watch_slot(ep);
    if (i < 0) return;
    w_hits[i]++;
    if ((int)w_hits[i] > w_cap) return;
    fprintf(watch_out(), "[WATCH] 0x%08x #%lu  esp=0x%08x ecx=0x%08x "
                    "ret=0x%08x arg0=0x%08x",
            ep, w_hits[i], C->esp, C->ecx,
            C->esp ? RD32(C->esp) : 0u,
            C->esp ? RD32(C->esp + 4) : 0u);
    if (w_mem) {
        uint32_t host = G_IMGBASE + (w_mem - 0x10000000u);
        fprintf(watch_out(), "  [0x%08x]=0x%08x", w_mem, RD32(host));
    }
    fprintf(watch_out(), "\n");
    fflush(watch_out());
}

static void x86_watch_report(void)
{
    int i;
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
