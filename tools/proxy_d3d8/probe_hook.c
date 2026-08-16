/*
 * The STOCK half of the oracle comparison: record the same guest functions the
 * port records, from inside the real game running under Wine.
 *
 * The engine already loads this DLL as its d3d8, which is the only reason this
 * is possible without a debugger -- we are simply code that is already in the
 * address space, and libIGMath.dll is a module in it. Each probed function has
 * its first bytes replaced with a jump to a generated stub (probe_stubs.S);
 * the bytes that were there move to a trampoline that ends by jumping back
 * past the patch.
 *
 * WHAT IS REFUSED RATHER THAN GUESSED. Every step that could be wrong is
 * checked against something independent, and a probe that fails any of them is
 * dropped BY NAME and counted, never installed half-way:
 *
 *   - the module must be loaded (GetModuleHandle), so an address is never
 *     computed from a base of zero;
 *   - the bytes at the entry point must equal the bytes Ghidra decoded, which
 *     is what proves the address really is that function in THIS build of the
 *     DLL -- a stale export or a different game version fails here instead of
 *     silently recording a neighbouring function;
 *   - VirtualProtect must succeed before anything is written.
 *
 * The install report prints installed AND skipped with the total, because "3
 * probes installed" out of seven and "7 of 7" produce identically shaped
 * streams -- one of them just has three probes that never fire, which on the
 * other side reads as a function the port never called.
 */
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "probe_rec.h"
#include "probe_table.h"

/* Generated: the stub bodies and the trampoline slots they call through. */
extern void    *probe_stub_table[];
extern void    *probe_tramp[];

void probe_enter(int idx, pr_u32 ecx, pr_u32 args);
void probe_leave(int idx);

static ProbeSink g_sink;
static int       g_ready;
static int       g_installed, g_skipped;
static pr_u32    g_seq[PROBE_COUNT ? PROBE_COUNT : 1];
static unsigned  g_calls[PROBE_COUNT ? PROBE_COUNT : 1];
static DWORD     g_thread;
static unsigned  g_foreign;        /* calls from a thread other than the first */
static unsigned  g_overflow;       /* nesting deeper than the pending stack */
static unsigned long long g_max = 400000;

static FILE *g_log;
static void plog(const char *fmt, ...)
{
    va_list ap;
    if (!g_log) return;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fflush(g_log);
}

/* ---- readable memory ------------------------------------------------------
 *
 * A probe dereferences pointers the engine gave it. Faulting inside the
 * recorder would crash the control run and destroy the capture, so each page
 * is checked once and cached. An unreadable field is recorded as
 * PROBE_UNREADABLE and counted -- never as zeros, which would make two sides
 * that both failed to read look like two sides that agreed.
 */

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
    MEMORY_BASIC_INFORMATION mbi;
    DWORD prot;
    if (VirtualQuery((LPCVOID)(UINT_PTR)page, &mbi, sizeof mbi) != sizeof mbi
            || mbi.State != MEM_COMMIT)
        return 0;
    prot = mbi.Protect & 0xffu;
    return prot == PAGE_READONLY || prot == PAGE_READWRITE
        || prot == PAGE_WRITECOPY || prot == PAGE_EXECUTE_READ
        || prot == PAGE_EXECUTE_READWRITE || prot == PAGE_EXECUTE_WRITECOPY;
}

/* ---- the recorder ---------------------------------------------------------
 *
 * probe_enter captures the arguments and pushes a frame; probe_leave pops it
 * and captures what the call wrote, using the ENTRY this-pointer. A stack
 * rather than one slot per probe, because probes can nest -- and because an
 * overflow must be counted rather than silently overwriting a frame.
 */
#define PENDING_N 16
static struct {
    int    idx;
    pr_u32 ecx, args;
    int    nin;
    unsigned char in[PROBE_MAX_RECORD];
} g_pending[PENDING_N];
static int g_depth;

void probe_enter(int idx, pr_u32 ecx, pr_u32 args)
{
    if (idx < 0 || idx >= PROBE_COUNT) return;
    g_calls[idx]++;
    if (g_thread == 0) g_thread = GetCurrentThreadId();
    else if (g_thread != GetCurrentThreadId()) g_foreign++;
    if (g_depth >= PENDING_N) { g_overflow++; g_depth++; return; }
    g_pending[g_depth].idx = idx;
    g_pending[g_depth].ecx = ecx;
    g_pending[g_depth].args = args;
    g_pending[g_depth].nin =
        (g_ready && g_sink.records < g_max)
        ? probe_capture(&g_probes[idx], PROBE_WHEN_IN, ecx, args,
                        g_pending[g_depth].in, PROBE_MAX_RECORD,
                        &g_sink.unreadable)
        : -2;                                     /* -2: not recording */
    g_depth++;
}

void probe_leave(int idx)
{
    unsigned char out[PROBE_MAX_RECORD];
    int nout;
    if (g_depth <= 0) return;
    g_depth--;
    if (g_depth >= PENDING_N) return;             /* its enter was dropped */
    if (g_pending[g_depth].idx != idx) return;    /* mismatched; drop it */
    if (g_pending[g_depth].nin < 0) return;
    nout = probe_capture(&g_probes[idx], PROBE_WHEN_OUT,
                         g_pending[g_depth].ecx, g_pending[g_depth].args,
                         out, sizeof out, &g_sink.unreadable);
    if (nout < 0) { g_sink.dropped++; return; }
    probe_emit(&g_sink, &g_probes[idx], g_seq[idx]++,
               g_pending[g_depth].in, g_pending[g_depth].nin, out, nout);
    if ((g_sink.records % 64) == 0 && g_sink.f) fflush(g_sink.f);
}

/* ---- installation --------------------------------------------------------- */

static int patch_one(int i, const Probe *p, HMODULE mod)
{
    unsigned char *at = (unsigned char *)mod + (p->linked - 0x10000000u);
    unsigned char *tramp;
    DWORD old;

    if (memcmp(at, p->expect, p->prologue) != 0) {
        int k;
        char got[64], want[64];
        got[0] = want[0] = 0;
        for (k = 0; k < p->prologue && k < 10; k++) {
            sprintf(got + strlen(got), "%02x", at[k]);
            sprintf(want + strlen(want), "%02x", p->expect[k]);
        }
        plog("probe: SKIP %s -- the bytes at %p are %s, Ghidra decoded %s.\n"
             "  That address is not the function this manifest describes, so "
             "patching it would record something else entirely.\n",
             p->name, (void *)at, got, want);
        return 0;
    }
    /* The trampoline: the bytes we are about to overwrite, then a jump back to
       the instruction after them. */
    tramp = VirtualAlloc(NULL, 64, MEM_COMMIT | MEM_RESERVE,
                         PAGE_EXECUTE_READWRITE);
    if (!tramp) {
        plog("probe: SKIP %s -- VirtualAlloc for a trampoline failed (%lu)\n",
             p->name, GetLastError());
        return 0;
    }
    memcpy(tramp, at, p->prologue);
    tramp[p->prologue] = 0xE9;
    *(int *)(tramp + p->prologue + 1) =
        (int)((at + p->prologue) - (tramp + p->prologue + 5));

    if (!VirtualProtect(at, p->prologue, PAGE_EXECUTE_READWRITE, &old)) {
        plog("probe: SKIP %s -- VirtualProtect failed (%lu); NOTHING was "
             "written at %p\n", p->name, GetLastError(), (void *)at);
        VirtualFree(tramp, 0, MEM_RELEASE);
        return 0;
    }
    probe_tramp[i] = tramp;
    at[0] = 0xE9;
    *(int *)(at + 1) = (int)((unsigned char *)probe_stub_table[i] - (at + 5));
    /* Anything between the jump and the end of the relocated region is now
       unreachable; fill it with INT3 so a stray branch faults loudly rather
       than running the tail of a half-overwritten instruction. */
    memset(at + 5, 0xCC, (size_t)(p->prologue - 5));
    VirtualProtect(at, p->prologue, old, &old);
    FlushInstructionCache(GetCurrentProcess(), at, p->prologue);
    plog("probe: %-52s at %p, %d byte(s) relocated\n",
         p->name, (void *)at, p->prologue);
    return 1;
}

void probe_hook_install(const char *logdir)
{
    char path[MAX_PATH];
    char env[MAX_PATH];
    int i;
    HMODULE mod;

    if (g_ready || g_installed || g_skipped) return;

    sprintf(path, "%s\\probe_stock.log", logdir);
    g_log = fopen(path, "w");

    if (PROBE_COUNT == 0) {
        plog("probe: tools/probes.json declares NO probes. Installed nothing; "
             "this run records nothing.\n");
        return;
    }
    if (GetEnvironmentVariableA("X2_PROBE_STOCK", env, sizeof env) == 0)
        sprintf(env, "%s\\probe_stock.bin", logdir);

    for (i = 0; i < PROBE_COUNT; i++) {
        char dll[64];
        sprintf(dll, "%s.dll", g_probes[i].module);
        mod = GetModuleHandleA(dll);
        if (!mod) {
            plog("probe: SKIP %s -- %s is not loaded in this process, so its "
                 "address cannot be computed.\n", g_probes[i].name, dll);
            g_skipped++;
            continue;
        }
        if (patch_one(i, &g_probes[i], mod)) g_installed++;
        else g_skipped++;
    }

    if (g_installed == 0) {
        plog("probe: 0 of %d probe(s) installed. This run will produce an "
             "EMPTY stream. An empty stream is not agreement -- do not compare "
             "it.\n", PROBE_COUNT);
        return;
    }
    if (!probe_sink_open(&g_sink, env, PROBE_MANIFEST_HASH,
                         PROBE_SIDE_STOCK, PROBE_COUNT)) {
        plog("probe: %d probe(s) are patched but %s could not be opened, so "
             "NOTHING is being recorded.\n", g_installed, env);
        return;
    }
    g_ready = 1;
    plog("probe: %d of %d installed, %d skipped; recording to %s "
         "(manifest 0x%08x)\n", g_installed, PROBE_COUNT, g_skipped, env,
         PROBE_MANIFEST_HASH);
}

/* Called from Present, so a run that is killed still has a report. */
void probe_hook_tick(void)
{
    if (g_sink.f) fflush(g_sink.f);
}

/* Reprinted periodically from Present, and once at detach: this process is
   always ended by a kill, so a count that only exists at exit does not
   exist. Idempotent -- it closes the stream only when it is still open. */
void probe_hook_report_counts(void)
{
    int i;
    plog("probe: per-probe call counts (0 is a result, not a blank):\n");
    for (i = 0; i < PROBE_COUNT; i++)
        plog("  %-52s %10u call(s), %10u recorded\n",
             g_probes[i].name, g_calls[i], (unsigned)g_seq[i]);
    if (g_foreign)
        plog("probe: %u call(s) came from a thread other than the first one "
             "seen. The pending stack is not per-thread, so those records may "
             "be interleaved -- treat this stream as suspect.\n", g_foreign);
    if (g_overflow)
        plog("probe: %u call(s) nested deeper than %d and were NOT recorded.\n",
             g_overflow, PENDING_N);
    plog("probe: %llu record(s), %llu unreadable field(s), %llu dropped\n",
         (unsigned long long)g_sink.records,
         (unsigned long long)g_sink.unreadable,
         (unsigned long long)g_sink.dropped);
    if (g_sink.records == 0)
        plog("probe: ZERO records. No probed function was ever called, which "
             "means the hooks did not take -- not that the two sides agree.\n");
    if (g_sink.f) { fclose(g_sink.f); g_sink.f = NULL; }
    g_ready = 0;
}
