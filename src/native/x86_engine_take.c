#include "x86_engine_take.h"

#include "x86rt_native.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TAKE_MAX 64
#define TAKE_NAME 64

typedef enum {
    kTakeNone = 0, /* nothing requested; dispatch is unchanged */
    kTakeSome,     /* the entries below */
    kTakeAll       /* every translated body the dispatcher reaches */
} TakeMode;

/*
 * One entry: a half-open mapped range [lo, hi).
 *
 * A single address is the range [a, a+1), and a module is the range it was
 * mapped into -- one representation rather than three, so membership is one
 * comparison and the report is one column. `name` is set only for a module,
 * whose range cannot be known until the loader has mapped it.
 */
typedef struct {
    char name[TAKE_NAME];
    uint32_t lo, hi;
    unsigned long routed; /* calls the dispatcher sent to the engine */
    unsigned long kept;   /* hand-backs the engine suppressed instead */
} TakeEntry;

static struct {
    TakeMode mode;
    TakeEntry e[TAKE_MAX];
    int n;
    unsigned long all_routed, all_kept;
} g_take;

/* ---- classification ---------------------------------------------------- */

/*
 * Host code, asked through the dispatcher's own published enumerations rather
 * than re-derived here.
 *
 * x86_thunk_name answers for the whole import-thunk range, and
 * x86_override_mapped_ep walks the resolved override table. Neither is a copy
 * of the dispatcher's policy -- they ARE that policy, read out -- so this
 * cannot drift from x86_native_call_at the way a second address-range constant
 * here would.
 */
static int is_host_code(uint32_t addr)
{
    const char *mod = NULL;
    int i;
    if (x86_thunk_name(addr, &mod)) return 1;
    for (i = 0;; i++) {
        uint32_t ep = x86_override_mapped_ep(i);
        if (!ep) return 0;
        if (ep == addr) return 1;
    }
}

/* ---- parsing ----------------------------------------------------------- */

static TakeEntry *fresh(char *reason, unsigned reason_len)
{
    if (g_take.n == TAKE_MAX) {
        snprintf(reason, reason_len,
                 "X2_ENGINE_TAKE names more than %d entries. Use a module "
                 "name, a lo-hi range, or `all`",
                 TAKE_MAX);
        return NULL;
    }
    return &g_take.e[g_take.n++];
}

/* A hex number, and nothing else. Returns 0 having left *out untouched. */
static int hex(const char *s, const char *end, uint32_t *out)
{
    char buf[32];
    char *stop = NULL;
    unsigned long v;
    size_t n = (size_t)(end - s);
    if (!n || n >= sizeof buf) return 0;
    memcpy(buf, s, n);
    buf[n] = '\0';
    v = strtoul(buf, &stop, 16);
    if (!stop || *stop || v == 0 || v > 0xFFFFFFFFul) return 0;
    *out = (uint32_t)v;
    return 1;
}

/*
 * One token: `0x401000`, `0x401000-0x402000`, or a module name.
 *
 * The module form is what makes a divergence bisectable: `all` is either
 * clean or broken somewhere, and halving a range by hand is the only way to
 * turn "broken somewhere" into an address. Refuses anything it cannot read by
 * showing the token, because a silently skipped entry is a take set that
 * measures something other than what was asked for.
 */
static int parse_token(const char *tok, const char *where, char *reason,
                       unsigned reason_len)
{
    const char *end, *dash;
    TakeEntry *e;
    uint32_t lo, hi;

    while (*tok == ' ' || *tok == '\t') tok++;
    end = tok + strlen(tok);
    while (end > tok && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r'))
        end--;
    if (end == tok) return 1; /* a separator or a blank line, not a token */

    if (!strncmp(tok, "0x", 2) || !strncmp(tok, "0X", 2)) tok += 2;
    dash = memchr(tok, '-', (size_t)(end - tok));
    if (dash) {
        const char *rhs = dash + 1;
        if (!strncmp(rhs, "0x", 2) || !strncmp(rhs, "0X", 2)) rhs += 2;
        if (!hex(tok, dash, &lo) || !hex(rhs, end, &hi) || hi <= lo) {
            snprintf(reason, reason_len,
                     "X2_ENGINE_TAKE%s: \"%s\" is not a lo-hi range of mapped "
                     "hex addresses",
                     where, tok);
            return 0;
        }
        if (!(e = fresh(reason, reason_len))) return 0;
        e->lo = lo;
        e->hi = hi;
        return 1;
    }
    if (hex(tok, end, &lo)) {
        if (!(e = fresh(reason, reason_len))) return 0;
        e->lo = lo;
        e->hi = lo + 1u;
        return 1;
    }
    /* Not a number, so a module name. Its range is resolved in x2_take_validate,
       because the loader has not mapped it yet when this runs. */
    if ((size_t)(end - tok) >= TAKE_NAME) {
        snprintf(reason, reason_len,
                 "X2_ENGINE_TAKE%s: \"%s\" is longer than a module name can be",
                 where, tok);
        return 0;
    }
    if (!(e = fresh(reason, reason_len))) return 0;
    memcpy(e->name, tok, (size_t)(end - tok));
    return 1;
}

static int parse_file(const char *path, char *reason, unsigned reason_len)
{
    char line[128];
    FILE *f = fopen(path, "r");
    int lineno = 0;
    if (!f) {
        snprintf(reason, reason_len,
                 "X2_ENGINE_TAKE names the file \"%s\", which cannot be opened",
                 path);
        return 0;
    }
    while (fgets(line, sizeof line, f)) {
        char *hash = strchr(line, '#');
        char where[96];
        lineno++;
        if (hash) *hash = '\0';
        line[strcspn(line, "\r\n")] = '\0';
        snprintf(where, sizeof where, " (%s line %d)", path, lineno);
        if (!parse_token(line, where, reason, reason_len)) {
            fclose(f);
            return 0;
        }
    }
    fclose(f);
    if (!g_take.n) {
        snprintf(reason, reason_len,
                 "X2_ENGINE_TAKE names the file \"%s\", which contains no "
                 "entries -- an empty take set takes nothing, which is not "
                 "what asking for one means",
                 path);
        return 0;
    }
    return 1;
}

int x2_take_init(int engine_selected, char *reason, unsigned reason_len)
{
    const char *spec = getenv("X2_ENGINE_TAKE");
    char buf[1024];
    char *tok, *save = NULL;

    memset(&g_take, 0, sizeof g_take);
    if (!spec || !*spec) return 1;

    if (!engine_selected) {
        snprintf(reason, reason_len,
                 "X2_ENGINE_TAKE asks the substrate to decline bodies, but no "
                 "engine is selected to run them. Set X2_ENGINE too");
        return 0;
    }
    if (!strcmp(spec, "all")) {
        g_take.mode = kTakeAll;
        return 1;
    }
    if (spec[0] == '@') {
        if (!parse_file(spec + 1, reason, reason_len)) return 0;
        g_take.mode = kTakeSome;
        return 1;
    }
    if (strlen(spec) >= sizeof buf) {
        snprintf(reason, reason_len,
                 "X2_ENGINE_TAKE is longer than %u characters. Use @<file> for "
                 "a list this size",
                 (unsigned)sizeof buf);
        return 0;
    }
    snprintf(buf, sizeof buf, "%s", spec);
    for (tok = strtok_r(buf, ",", &save); tok; tok = strtok_r(NULL, ",", &save))
        if (!parse_token(tok, "", reason, reason_len)) return 0;
    if (!g_take.n) {
        snprintf(reason, reason_len,
                 "X2_ENGINE_TAKE is set to \"%s\", which names no entries",
                 spec);
        return 0;
    }
    g_take.mode = kTakeSome;
    return 1;
}

/* ---- membership -------------------------------------------------------- */

int x2_take_has(uint32_t addr, X2TakeSite site)
{
    int i;
    if (g_take.mode == kTakeNone) return 0;
    /* Host code first, in BOTH modes. `all` means every address with guest
       bytes to run, and a thunk or an override has none. */
    if (is_host_code(addr)) return 0;
    if (g_take.mode == kTakeAll) {
        if (!x86_native_body_at(addr)) return 0;
        if (site == kX2TakeDispatch) g_take.all_routed++;
        else g_take.all_kept++;
        return 1;
    }
    for (i = 0; i < g_take.n; i++)
        if (addr >= g_take.e[i].lo && addr < g_take.e[i].hi) {
            if (site == kX2TakeDispatch) g_take.e[i].routed++;
            else g_take.e[i].kept++;
            return 1;
        }
    return 0;
}

/* ---- validation -------------------------------------------------------- */

/* Fill in a module entry's mapped range, now that the loader has mapped it. */
static int resolve_module(TakeEntry *e)
{
    X86Module *m;
    for (m = x86_modules(); m; m = m->next)
        if (m->name && !strcasecmp(m->name, e->name)) {
            e->lo = *m->base;
            e->hi = *m->base + m->size;
            return 1;
        }
    fprintf(stderr,
            "[TAKE] \"%s\" is not a registered module. The names are the ones "
            "the loader prints, e.g. XMen2.exe or msdia80.dll.\n",
            e->name);
    return 0;
}

/* How many translated bodies a range actually contains. A range with none is
   a typo that would take nothing while looking like it took something. */
static int bodies_in(const TakeEntry *e)
{
    X86Module *m;
    int i, n = 0;
    for (m = x86_modules(); m; m = m->next)
        for (i = 0; i < m->nfns; i++) {
            const uint32_t a = *m->base + (m->fns[i].ep - m->preferred);
            if (a >= e->lo && a < e->hi && !is_host_code(a)) n++;
        }
    return n;
}

int x2_take_validate(void)
{
    int i, bad = 0;
    if (g_take.mode != kTakeSome) return 1;
    for (i = 0; i < g_take.n; i++) {
        TakeEntry *e = &g_take.e[i];
        int n;
        if (e->name[0] && !resolve_module(e)) {
            bad++;
            continue;
        }
        if (e->hi == e->lo + 1u) {
            /* A single address: it must be a body, and not a host one. */
            if (is_host_code(e->lo)) {
                fprintf(stderr,
                        "[TAKE] 0x%08x is an import thunk or a native "
                        "override: it is a C function, and there are no guest "
                        "bytes at that address for the engine to run.\n",
                        e->lo);
                bad++;
            } else if (!x86_native_body_at(e->lo)) {
                fprintf(stderr,
                        "[TAKE] 0x%08x has no body for the substrate to "
                        "decline. Take addresses are MAPPED entry points, not "
                        "the guest addresses a disassembly lists.\n",
                        e->lo);
                bad++;
            }
            continue;
        }
        n = bodies_in(e);
        if (!n) {
            fprintf(stderr,
                    "[TAKE] %s0x%08x-0x%08x contains no translated body for "
                    "the substrate to decline.\n",
                    e->name[0] ? e->name : "", e->lo, e->hi);
            bad++;
        } else {
            fprintf(stderr, "[TAKE] %s0x%08x-0x%08x: %d body(s) eligible.\n",
                    e->name[0] ? e->name : "", e->lo, e->hi, n);
        }
    }
    if (bad) {
        fprintf(stderr,
                "[TAKE] %d of %d requested entry(s) cannot be taken; the run "
                "would measure a different set than the one asked for.\n",
                bad, g_take.n);
        return 0;
    }
    fprintf(stderr,
            "[TAKE] %d entry(s) will be declined by the substrate and run by "
            "the engine.\n",
            g_take.n);
    return 1;
}

/* ---- report ------------------------------------------------------------ */

void x2_take_report(void)
{
    int i;
    if (g_take.mode == kTakeNone) {
        fprintf(stderr,
                "[TAKE] nothing requested: the substrate kept every body it "
                "has, so the engine saw only what it could not translate.\n");
        return;
    }
    if (g_take.mode == kTakeAll) {
        fprintf(stderr,
                "[TAKE] all: %lu dispatch(es) routed to the engine, and %lu "
                "hand-back(s) it kept instead of returning to the "
                "substrate.\n",
                g_take.all_routed, g_take.all_kept);
        return;
    }
    for (i = 0; i < g_take.n; i++) {
        const TakeEntry *e = &g_take.e[i];
        const char *nm = e->hi == e->lo + 1u ? x86_native_name_at(e->lo) : NULL;
        fprintf(stderr,
                "[TAKE]   %-14s 0x%08x-0x%08x %-24s %lu routed, %lu kept\n",
                e->name[0] ? e->name : "", e->lo, e->hi, nm ? nm : "",
                e->routed, e->kept);
    }
    fprintf(stderr,
            "[TAKE] `routed` is calls the dispatcher gave the engine and must "
            "match its own call count; `kept` is hand-backs the engine "
            "suppressed because the target was taken too. An entry showing 0 "
            "for both was never reached, so it measured nothing -- which is "
            "not the same as running correctly.\n");
}
