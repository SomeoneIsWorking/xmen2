/* The reached set -- see x86_reached.h for what this instrument is and why
 * it owns the /reached endpoint. Extracted from x86rt_native.c: the runtime
 * file had grown past its frozen structure limit, and this was the one
 * cohesive owner inside it. The hot-path hook (x86_reached_enter) is called
 * from the generated bodies' entry macro (x86rt.h) and stays declared there.
 */
#include "x86_reached.h"

#include "control.h"
#include "control_query.h"
#include "x86rt_native.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

/*
 * Armed, not compiled in. X86_NATIVE_REACHED arms it before the first guest
 * instruction; X2_REACHED and the control channel arm it later, which is
 * enough for anything that happens after boot -- and the report says WHEN it
 * was armed, because a NEVER from an instrument that was armed halfway
 * through a run is not a NEVER.
 */
volatile int x86_reached_armed
#ifdef X86_NATIVE_REACHED
    = 1
#endif
    ;
static const char *g_reached_why;             /* what armed it, for the report */

void x86_reached_arm(const char *why)
{
    if (x86_reached_armed) return;
    g_reached_why = why;
    x86_reached_armed = 1;
    fprintf(stderr, "[REACHED] armed (%s). Everything entered BEFORE this "
                    "point is invisible to it, so a NEVER below is a claim "
                    "about the run from here on.\n", why ? why : "no reason "
                    "given");
}

int x86_reached_is_armed(void) { return x86_reached_armed; }

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

int x86_reached_query(uint32_t linked_ep, const char *module,
                      unsigned long *count, unsigned long *seq)
{
    X86Module *m;
    int found = 0;
    if (count) *count = 0;
    if (seq) *seq = 0;
    for (m = x86_modules(); m; m = m->next) {
        uint32_t s;
        if (module && (!m->name || strcmp(m->name, module))) continue;
        /* Only modules whose LINKED range holds this address: without the
           range test the subtraction wraps and probes a random address in
           every other module. */
        if (linked_ep < m->preferred || linked_ep - m->preferred >= m->size)
            continue;
        if (!x86_native_name_at(*m->base + (linked_ep - m->preferred)))
            continue;
        s = reached_seq(linked_ep, *m->base);
        if (!s) continue;
        found = 1;
        if (count) *count += reached_count(linked_ep, *m->base);
        if (seq && (!*seq || s < *seq)) *seq = s;
    }
    return found;
}

void x86_reached_report(void)
{
    const char *want = getenv("X2_REACHED");
    char buf[1024], *p, *save;
    if (getenv("X2_REACHED_SELFTEST")) reached_selftest();
    if (!x86_reached_armed) {
        fprintf(stderr, "[REACHED] NOT ARMED for this run, so it saw nothing "
                        "and can answer nothing. Arm it with X2_REACHED=<list "
                        "or 'all'>, tools/x2ctl.py reached --arm, or build "
                        "with -DX2_NATIVE_REACHED=ON to arm before the first "
                        "guest instruction.\n");
        return;
    }
    fprintf(stderr, "[REACHED] %u distinct (entry point, module) pair(s) were "
                    "entered%s%s.\n", g_reached_n,
            g_reached_why ? ", armed by " : " (armed from the first "
                                            "instruction)",
            g_reached_why ? g_reached_why : "");
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




void x86_reached_arm_from_env(void)
{
    /* X2_REACHED names addresses to ASK about at the end; asking implies
       arming, because the alternative is a run that collects nothing and
       reports NEVER for everything. */
    if (getenv("X2_REACHED") || getenv("X2_REACHED_SELFTEST"))
        x86_reached_arm("X2_REACHED in the environment");
}

/*
 * /reached -- ask the RUNNING game whether it has ever entered a function.
 *
 * This is the endpoint the port was missing. Answering "does the engine call
 * igBitmapFont::getCharWidth?" used to mean a throwaway native override, a
 * module re-emit and a 272MB relink -- an hour to ask a yes/no question, so
 * the question got answered by reading code and guessing instead. Now it is
 * one request against a live run.
 *
 *   /reached?arm=1                 start recording (says what it missed)
 *   /reached?ep=0x1000e8c0         verdict for one linked entry point
 *   /reached?ep=0x...&module=libIGGui.dll   ... in one module only
 *
 * A verdict for an UNARMED run is refused rather than returned as "never":
 * "no" from an instrument that was not running is the exact shape of the
 * false negative this whole endpoint exists to stop producing.
 */
void x86_reached_route(int fd, const char *query)
{
    char arm[8] = "", ep_text[32] = "", module[64] = "";
    char body[1024];
    unsigned long count = 0, seq = 0;
    uint32_t ep;
    char *end;
    int armed;

    if (control_query_arg(query, "arm", arm, sizeof arm) && arm[0] != '0')
        x86_reached_arm("the live control channel");
    armed = x86_reached_is_armed();

    if (!control_query_arg(query, "ep", ep_text, sizeof ep_text)) {
        snprintf(body, sizeof body,
                 "reached set: %s\n"
                 "Ask about one function with /reached?ep=0x1000e8c0 "
                 "[&module=libIGGui.dll].\n"
                 "The entry point is the LINKED address -- what the "
                 "disassembly shows.\n",
                 armed ? "ARMED and recording" : "NOT armed; arm it with "
                                                 "/reached?arm=1");
        control_reply_text(fd, 200, "OK", "%s", body);
        return;
    }
    ep = (uint32_t)strtoul(ep_text, &end, 0);
    if (*end || !ep) {
        control_reply_text(fd, 400, "Bad Request",
                   "%s is not an entry point. Use /reached?ep=0x1000e8c0.\n",
                   ep_text);
        return;
    }
    if (!armed) {
        control_reply_text(fd, 409, "Conflict",
                   "the reached set is NOT ARMED, so it has recorded nothing "
                   "and cannot say whether 0x%08x ran.\n"
                   "Arm it (/reached?arm=1) and ask again -- but note that "
                   "anything entered BEFORE arming stays invisible, so for "
                   "boot-time questions launch with X2_REACHED set.\n", ep);
        return;
    }
    control_query_arg(query, "module", module, sizeof module);
    if (x86_reached_query(ep, module[0] ? module : NULL, &count, &seq))
        control_reply_text(fd, 200, "OK",
                   "0x%08x REACHED  %lu call(s), first at #%lu%s%s\n",
                   ep, count, seq, module[0] ? " in " : "", module);
    else
        control_reply_text(fd, 200, "OK",
                   "0x%08x NEVER entered since the set was armed%s%s\n",
                   ep, module[0] ? " in " : "", module);
}

