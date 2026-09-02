#include "x86_engine_take.h"

#include "x86rt_native.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TAKE_MAX 256

typedef enum {
    kTakeNone = 0, /* nothing requested; dispatch is unchanged */
    kTakeList,     /* the addresses named below */
    kTakeAll       /* every translated body the dispatcher reaches */
} TakeMode;

static struct {
    TakeMode mode;
    uint32_t addr[TAKE_MAX];
    int n;
    int overflowed;
    unsigned long hits[TAKE_MAX]; /* per named address, so a set that fired
                                     and one that did not are different runs */
    unsigned long all_hits;
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

static int index_of(uint32_t addr)
{
    int i;
    for (i = 0; i < g_take.n; i++)
        if (g_take.addr[i] == addr) return i;
    return -1;
}

/* ---- parsing ----------------------------------------------------------- */

static int add(uint32_t addr, char *reason, unsigned reason_len)
{
    if (index_of(addr) >= 0) return 1; /* named twice is not two takes */
    if (g_take.n == TAKE_MAX) {
        snprintf(reason, reason_len,
                 "X2_ENGINE_TAKE names more than %d addresses. Raise TAKE_MAX, "
                 "or use `all` if the intent is the whole corpus",
                 TAKE_MAX);
        g_take.overflowed = 1;
        return 0;
    }
    g_take.addr[g_take.n++] = addr;
    return 1;
}

/* One token: a hex address, with or without 0x. Refuses anything else by
   showing the token, because a silently skipped entry is a take set that
   measures something other than what was asked for. */
static int parse_token(const char *tok, const char *where, char *reason,
                       unsigned reason_len)
{
    char *end = NULL;
    unsigned long v;
    while (*tok == ' ' || *tok == '\t') tok++;
    if (!*tok) return 1; /* trailing separator, not a token */
    v = strtoul(tok, &end, 16);
    while (end && (*end == ' ' || *end == '\t' || *end == '\r')) end++;
    if (!end || *end || end == tok || v == 0 || v > 0xFFFFFFFFul) {
        snprintf(reason, reason_len,
                 "X2_ENGINE_TAKE%s: \"%s\" is not a mapped hex address",
                 where, tok);
        return 0;
    }
    return add((uint32_t)v, reason, reason_len);
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
        char where[64];
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
                 "addresses -- an empty take set takes nothing, which is not "
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
        g_take.mode = kTakeList;
        return 1;
    }
    if (strlen(spec) >= sizeof buf) {
        snprintf(reason, reason_len,
                 "X2_ENGINE_TAKE is longer than %u characters. Use "
                 "@<file> for a list this size",
                 (unsigned)sizeof buf);
        return 0;
    }
    snprintf(buf, sizeof buf, "%s", spec);
    for (tok = strtok_r(buf, ",", &save); tok;
         tok = strtok_r(NULL, ",", &save))
        if (!parse_token(tok, "", reason, reason_len)) return 0;
    if (!g_take.n) {
        snprintf(reason, reason_len,
                 "X2_ENGINE_TAKE is set to \"%s\", which names no addresses",
                 spec);
        return 0;
    }
    g_take.mode = kTakeList;
    return 1;
}

/* ---- membership -------------------------------------------------------- */

int x2_take_has(uint32_t addr)
{
    int i;
    if (g_take.mode == kTakeNone) return 0;
    /* Host code first, in BOTH modes. `all` means every address with guest
       bytes to run, and a thunk or an override has none. */
    if (is_host_code(addr)) return 0;
    if (g_take.mode == kTakeAll) {
        if (!x86_native_body_at(addr)) return 0;
        g_take.all_hits++;
        return 1;
    }
    i = index_of(addr);
    if (i < 0) return 0;
    g_take.hits[i]++;
    return 1;
}

/* ---- validation -------------------------------------------------------- */

int x2_take_validate(void)
{
    int i, bad = 0;
    if (g_take.mode != kTakeList) return 1;
    for (i = 0; i < g_take.n; i++) {
        const uint32_t a = g_take.addr[i];
        if (is_host_code(a)) {
            fprintf(stderr,
                    "[TAKE] 0x%08x is an import thunk or a native override: it "
                    "is a C function, and there are no guest bytes at that "
                    "address for the engine to run.\n",
                    a);
            bad++;
        } else if (!x86_native_body_at(a)) {
            fprintf(stderr,
                    "[TAKE] 0x%08x has no body for the substrate to decline. "
                    "Take addresses are MAPPED entry points, not the guest "
                    "addresses a disassembly lists.\n",
                    a);
            bad++;
        }
    }
    if (bad) {
        fprintf(stderr,
                "[TAKE] %d of %d requested address(es) cannot be taken; the "
                "run would measure a different set than the one asked for.\n",
                bad, g_take.n);
        return 0;
    }
    fprintf(stderr,
            "[TAKE] %d entry point(s) will be declined by the substrate and "
            "run by the engine.\n",
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
                "[TAKE] all: %lu dispatch(es) of a translated body were handed "
                "to the engine.\n",
                g_take.all_hits);
        return;
    }
    for (i = 0; i < g_take.n; i++)
        fprintf(stderr, "[TAKE]   0x%08x %-40s %lu dispatch(es)\n",
                g_take.addr[i],
                x86_native_name_at(g_take.addr[i])
                    ? x86_native_name_at(g_take.addr[i])
                    : "(unnamed)",
                g_take.hits[i]);
    fprintf(stderr,
            "[TAKE] a requested address showing 0 was never reached this run, "
            "so it measured nothing -- that is not the same as running "
            "correctly.\n");
}
