/*
 * The PORT half of the oracle comparison: record every call to a probed guest
 * function, in the format tools/proxy_d3d8 writes on the other side.
 *
 * The hook is ld --wrap. src/recomp/gen/probe_wraps.c holds one
 * __wrap_fn_<module>_<ep> per probe; each calls in here with the real body,
 * and this records around it and changes nothing. That mechanism -- not the
 * dispatcher -- because the recompiler emits an intra-module call as a direct
 * C call to fn_<module>_<ep>, which never passes through x86_native_call_at. A
 * dispatcher hook would have recorded only the calls that cross a module, and
 * reported the rest as "never called".
 *
 * ARMING. Off unless X2_PROBE names a file. A run without it pays one
 * predictable branch per probed call and nothing else, so the probed build and
 * the shipping build are the same binary.
 *
 * The stream is flushed every PROBE_FLUSH records because nothing in this
 * program stops on its own -- every run here ends in a timeout or a kill, and
 * a stream that is only complete after a clean exit would be empty exactly
 * when it is needed. The same reason the heartbeat carries a live count
 * instead of a shutdown report.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>

#include "x86rt.h"
#include "x86rt_native.h"
#include "guest_memory.h"
#define PROBE_GUEST_POINTER(address) guest_memory_const_pointer(address)
#include "probe_table.h"
#include "oracle_trace.h"

#define PROBE_FLUSH 64

static ProbeSink g_sink;
static int   g_armed;              /* 1 once the file is open */
static int   g_off;                /* 1 once we have decided not to record */
static pr_u32 g_seq[PROBE_COUNT ? PROBE_COUNT : 1];
static unsigned long long g_calls; /* probed calls SEEN, armed or not */
static unsigned long long g_capped;
static unsigned long long g_max = 400000;
static int g_probe_selftest;

/* ---- readable guest memory -------------------------------------------------
 *
 * A probe dereferences pointers the guest gave it, and a guest that is already
 * misbehaving can hand over a bad one. Faulting inside the recorder would turn
 * a diagnostic into the crash it was meant to explain, so every page is
 * checked before it is read -- once, then cached, because these functions are
 * called thousands of times a frame and mincore() is a syscall.
 *
 * A page that is not mapped is NOT silently skipped: probe_capture writes
 * PROBE_UNREADABLE across the field and counts it, so "could not read" and
 * "read zeros" stay different in the stream.
 */
#if defined(__APPLE__) && defined(__aarch64__)
#define PAGE_BITS 14
#else
#define PAGE_BITS 12
#endif
#define PAGE_SZ   (1u << PAGE_BITS)
/*
 * NOT CACHED, deliberately.
 *
 * The first version remembered which pages were readable, because these
 * functions are called tens of thousands of times a frame and this is a
 * syscall. That is unsound: the game frees memory, so a page cached as
 * readable can be unmapped by the time it is next read, and the recorder --
 * whose entire job is to observe without disturbing -- then faults inside the
 * run it is measuring. The selftest below crashed on exactly that, which is
 * the only reason it is not still in here.
 *
 * The cost was measured rather than assumed: a driven capture makes about
 * 400,000 of these calls over two minutes, which is a fraction of a second.
 */
int probe_page_readable(pr_u32 page)
{
    return guest_memory_is_readable(page, PAGE_SZ);
}

/* ---- arming --------------------------------------------------------------- */

static void arm_once(void)
{
    const char *path = getenv("X2_PROBE");
    const char *cap = getenv("X2_PROBE_MAX");
    if (g_armed || g_off) return;
    if (!path || !*path) {
        g_off = 1;
        return;
    }
    if (cap && *cap) {
        char *e;
        unsigned long long v = strtoull(cap, &e, 0);
        if (*e || v == 0) {
            fprintf(stderr, "oracle_trace: X2_PROBE_MAX=%s is not a positive "
                            "number. Recording NOTHING rather than guessing a "
                            "cap.\n", cap);
            g_off = 1;
            return;
        }
        g_max = v;
    }
    if (!probe_sink_open(&g_sink, path, PROBE_MANIFEST_HASH,
                         PROBE_SIDE_PORT, PROBE_COUNT)) {
        fprintf(stderr, "oracle_trace: cannot write %s -- recording NOTHING. "
                        "This is not an empty capture, it is no capture.\n",
                path);
        g_off = 1;
        return;
    }
    fprintf(stderr, "oracle_trace: recording %d probe(s) to %s "
                    "(manifest 0x%08x, cap %llu records)\n",
            PROBE_COUNT, path, PROBE_MANIFEST_HASH, g_max);
    g_armed = 1;
    if (!g_probe_selftest) oracle_probe_binding_check();
}

/*
 * Did the hooks actually bind?
 *
 * ld --wrap only redirects references that CROSS an object file, so a probed
 * function emitted into the same translation unit as its caller is bound at
 * compile time: the build links, nothing complains, and the stream comes out
 * empty -- which on the other side of a comparison reads as agreement. That is
 * the failure this whole harness must not have, so it is checked in the
 * shipping binary rather than argued about.
 *
 * Grepping the disassembly for call sites is NOT the check. These functions
 * are exported and called from other modules, so most of their callers reach
 * them through the guest's own import tables and the dispatcher, not through a
 * direct call -- an intra-module count of zero is normal. What must be true is
 * that the entry the DISPATCHER resolves for this address is the wrapper.
 */
extern void (*const g_probe_wrapfn[PROBE_COUNT])(CPU *);

int oracle_probe_binding_check(void)
{
    int i, bad = 0;
    for (i = 0; i < PROBE_COUNT; i++) {
        const Probe *p = &g_probes[i];
        X86Module *m;
        const X86Fn *found = NULL;
        int k;
        /* The generated modules register themselves as "libIGMath.dll"; the
           manifest names them "libIGMath", which is what the Ghidra export and
           the stock side's GetModuleHandleA both use. Accept either rather
           than reporting a module that is right there as missing. */
        for (m = x86_modules(); m; m = m->next) {
            size_t n = strlen(p->module);
            if (strncmp(m->name, p->module, n) == 0
                    && (m->name[n] == 0 || strcmp(m->name + n, ".dll") == 0 ||
                        strcmp(m->name + n, ".exe") == 0))
                break;
        }
        if (!m) {
            fprintf(stderr, "  probe %-44s module %s IS NOT LINKED into this "
                            "build; it can never fire.\n",
                    p->name, p->module);
            bad++;
            continue;
        }
        for (k = 0; k < m->nfns; k++)
            if (m->fns[k].ep == p->linked) { found = &m->fns[k]; break; }
        if (!found) {
            fprintf(stderr, "  probe %-44s no entry at 0x%08x in %s's table; "
                            "the address is wrong.\n",
                    p->name, p->linked, p->module);
            bad++;
        }
#if defined(__APPLE__)
        else if (!x86_override_is_bound(p->image, p->linked,
                                        g_probe_wrapfn[i])) {
            fprintf(stderr,
                    "  probe %-44s NOT BOUND through its Mach-O override "
                    "slot; it cannot fire.\n", p->name);
            bad++;
        }
#else
        else if (found->fn != g_probe_wrapfn[i]) {
            fprintf(stderr,
                    "  probe %-44s NOT WRAPPED: the table entry is %p, the "
                    "wrapper is %p.\n"
                    "        --wrap bound at compile time, which means this "
                    "function was not isolated into its own translation unit. "
                    "Re-run tools/gen_probes.py and re-emit %s.\n",
                    p->name, (void *)found->fn, (void *)g_probe_wrapfn[i],
                    p->module);
            bad++;
        }
#endif
    }
    fprintf(stderr, "oracle_trace: %d of %d probe(s) bound to their wrapper%s\n",
            PROBE_COUNT - bad, PROBE_COUNT,
            bad ? " -- the rest CANNOT fire, and their absence from the stream "
                  "is not agreement" : "");
    return bad;
}

/*
 * Arm at STARTUP, not on the first probed call.
 *
 * Lazy arming was wrong in the way this harness must never be wrong: a run in
 * which no probed function is ever reached created no file, printed no binding
 * report, and left the heartbeat claiming recording was off. All three are the
 * signature of a harness that did not install, and all three were invisible
 * until the comparison stage -- where an absent stream reads as agreement.
 * Arming up front means the file, the manifest hash and the 7-of-7 binding
 * line all exist before the first frame, so "armed and nothing fired" and
 * "never armed" are different from the outset.
 */
void oracle_probe_arm(void)
{
    arm_once();
    if (g_off)
        fprintf(stderr, "oracle_trace: X2_PROBE is not set, so no oracle "
                        "probe stream is being recorded this run.\n");
}

/* ---- the hook ------------------------------------------------------------- */

void oracle_probe_call(const Probe *p, void (*real)(CPU *), CPU *C)
{
    unsigned char in[PROBE_MAX_RECORD], out[PROBE_MAX_RECORD];
    pr_u32 ecx, args;
    int nin, nout;

    g_calls++;
    if (!g_armed) {
        arm_once();
        if (!g_armed) { real(C); return; }
    }
    if (g_sink.records >= g_max) {
        g_capped++;
        real(C);
        return;
    }

    /* Saved at ENTRY. An `out` field still means "at the this-pointer this
       call was given", even though the body is free to clobber ECX and to
       move ESP -- which every one of these does. */
    ecx = C->ecx;
    args = C->esp + 4u;

    nin = probe_capture(p, PROBE_WHEN_IN, ecx, args, in, sizeof in,
                        &g_sink.unreadable);
    real(C);
    nout = probe_capture2(p, PROBE_WHEN_OUT, ecx, args, C->eax, out,
                          sizeof out, &g_sink.unreadable);
    if (nin < 0 || nout < 0) {
        /* The manifest declares more bytes than a record can hold. Silence
           here would look like a function that was never called. */
        static int said;
        if (!said) {
            said = 1;
            fprintf(stderr, "oracle_trace: probe %s declares more than %d "
                            "bytes; its records are DROPPED, not truncated.\n",
                    p->name, PROBE_MAX_RECORD);
        }
        g_sink.dropped++;
        return;
    }
    probe_emit(&g_sink, p, g_seq[p->id]++, in, nin, out, nout);
    if ((g_sink.records % PROBE_FLUSH) == 0 && g_sink.f) fflush(g_sink.f);
}

/* ---- reporting ------------------------------------------------------------
 *
 * Printed live, in the heartbeat, and AT ZERO. "0 of 7 probes fired" is the
 * single most useful thing this can say: it means the --wrap did not take, and
 * without it a comparison against an empty stream reads as agreement.
 */
int oracle_probe_line(char *buf, int n)
{
    int fired = 0, i;
    for (i = 0; i < PROBE_COUNT; i++) if (g_seq[i]) fired++;
    if (!g_armed)
        return snprintf(buf, (size_t)n,
                        "probe NOT recording (X2_PROBE unset); %llu probed "
                        "call(s) went by", g_calls);
    return snprintf(buf, (size_t)n,
                    "probe %d of %d fired, %llu rec, %llu call, %llu over cap",
                    fired, PROBE_COUNT, (unsigned long long)g_sink.records,
                    g_calls, g_capped);
}

void oracle_probe_report(void)
{
    int i;
    if (!g_armed) {
        if (g_calls)
            fprintf(stderr, "oracle_trace: %llu probed call(s) happened with "
                            "recording OFF (X2_PROBE unset).\n", g_calls);
        return;
    }
    fprintf(stderr, "oracle_trace: per-probe call counts (0 is a result, not "
                    "a blank):\n");
    for (i = 0; i < PROBE_COUNT; i++)
        fprintf(stderr, "  %-52s %10u\n", g_probes[i].name,
                (unsigned)g_seq[i]);
    if (g_capped)
        fprintf(stderr, "oracle_trace: %llu call(s) past the %llu-record cap "
                        "were NOT recorded. The stream is a prefix of the run, "
                        "not a sample of it.\n", g_capped, g_max);
    probe_sink_close(&g_sink, "port");
    g_armed = 0;
}

/*
 * PROOF THAT THE RECORDER FIRES -- x2native --probe-selftest.
 *
 * The failure this guards against is the whole harness running, writing a
 * well-formed file, and recording nothing because the wraps did not bind. That
 * looks exactly like two sides that agree. So: drive one probe by hand through
 * the same path a real call takes, with a known this-pointer and known bytes
 * behind it, and check the bytes come back out of the stream.
 */
static void selftest_body(CPU *C) { (void)C; }

int oracle_probe_selftest(void)
{
    const char *path = "scratch/logs/probe_selftest.bin";
    unsigned char want[16];
    static CPU C;
    FILE *f;
    long sz;
    unsigned char *buf;
    int i, fails = 0;
    pr_u32 obj;

    g_probe_selftest = 1;

    if (PROBE_COUNT == 0) {
        printf("probe selftest: tools/probes.json declares NO probes, so there "
               "is nothing to prove. That is a FAILURE of this test, not a "
               "pass.\n");
        return 1;
    }
    /* Somewhere real to point at: a page we own, so probe_read must succeed. */
    if (guest_memory_map_any(0x68000000u, 0x6f000000u, PAGE_SZ, PAGE_SZ,
                             PROT_READ | PROT_WRITE, &obj) != 0) {
        printf("probe selftest: could not map a low page -- ran NOTHING\n");
        return 1;
    }
    buf = guest_memory_pointer(obj);
    for (i = 0; i < 16; i++) want[i] = (unsigned char)(0xA0 + i);
    memcpy(buf, want, 16);

    /* The page walk itself, before anything that depends on it. The stock
       side's copy of this arithmetic compared a page-aligned end against an
       unaligned start, so EVERY read failed and a whole capture came back as
       24 unreadable fields out of 24. There is one copy now; this is what
       proves it walks pages rather than only ever reading within one. */
    {
        unsigned char probe_buf[32];
        pr_u32 cross = obj + PAGE_SZ - 8u;      /* 8 bytes either side */
        pr_u32 big_address;
        unsigned char *big;
        if (guest_memory_map_any(0x68000000u, 0x6f000000u, PAGE_SZ,
                                 PAGE_SZ * 2u, PROT_READ | PROT_WRITE,
                                 &big_address) != 0) {
            printf("  FAIL  could not map two pages; the walk is UNTESTED\n");
            fails++;
        } else {
            int k;
            big = guest_memory_pointer(big_address);
            for (k = 0; k < 32; k++) big[PAGE_SZ - 8 + k] = (unsigned char)k;
            cross = big_address + PAGE_SZ - 8u;
            if (!probe_read(cross, probe_buf, 32)) {
                printf("  FAIL  a 32-byte read spanning a page boundary was "
                       "refused; every field larger than the tail of a page "
                       "would record as UNREADABLE\n");
                fails++;
            } else if (memcmp(probe_buf, big + PAGE_SZ - 8, 32) != 0) {
                printf("  FAIL  the cross-page read returned the wrong bytes\n");
                fails++;
            } else {
                printf("  ok    a read spanning two pages returns both\n");
            }
            guest_memory_release(big_address, PAGE_SZ * 2u);
            /* And now that it is unmapped, the SAME read must be refused --
               a checker that says yes to everything is not a checker. */
            if (probe_read(cross, probe_buf, 32)) {
                printf("  FAIL  a read of UNMAPPED memory was allowed; the "
                       "page check answers yes to everything\n");
                fails++;
            } else {
                printf("  ok    the same read is refused once unmapped\n");
            }
        }
    }

    setenv("X2_PROBE", path, 1);
    g_armed = g_off = 0;
    memset(g_seq, 0, sizeof g_seq);
    memset(&g_sink, 0, sizeof g_sink);

    /* A frame whose this-pointer and first argument both point at the page. */
    cpu_reset(&C);
    C.esp = obj + 0x800u;
    C.ecx = obj;
    for (i = 0; i < 4; i++) {
        *(pr_u32 *)guest_memory_pointer(
            C.esp + 4u + (pr_u32)i * 4u) = obj;
    }
    oracle_probe_call(&g_probes[0], selftest_body, &C);

    if (g_sink.f) fflush(g_sink.f);
    f = fopen(path, "rb");
    if (!f) {
        printf("  FAIL  the stream %s was not created\n", path);
        return 1;
    }
    fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
    if (sz <= PROBE_MAGIC_N + 16) {
        printf("  FAIL  the stream holds a header and NO records (%ld bytes). "
               "The recorder ran and recorded nothing, which is the exact "
               "failure this test exists to catch.\n", sz);
        fclose(f);
        return 1;
    }
    {
        unsigned char *all = malloc((size_t)sz);
        size_t got = fread(all, 1, (size_t)sz, f);
        int found = 0;
        if (got == (size_t)sz) {
            for (i = 0; i + 16 <= (int)sz; i++)
                if (memcmp(all + i, want, 16) == 0) { found = 1; break; }
        }
        if (!found) {
            printf("  FAIL  the 16 known bytes behind the this-pointer are NOT "
                   "in the stream; the recorder wrote something else.\n");
            fails++;
        } else {
            printf("  ok    a probed call reached the stream, with the bytes "
                   "the guest memory held\n");
        }
        free(all);
    }
    fclose(f);
    printf("  ok    %llu record(s), %llu unreadable field(s)\n",
           (unsigned long long)g_sink.records,
           (unsigned long long)g_sink.unreadable);
    probe_sink_close(&g_sink, "selftest");
    guest_memory_release(obj, PAGE_SZ);
    unsetenv("X2_PROBE");
    g_armed = 0; g_off = 0;
    g_probe_selftest = 0;
    printf("probe selftest: %s -- %d failure(s)\n",
           fails ? "FAILED" : "PASSED", fails);
    return fails ? 1 : 0;
}
