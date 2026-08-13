/*
 * Region recording: what a stretch of guest code ACTUALLY did.
 *
 * WHY. A block can be unportable from the listing alone. The case that forced
 * this one exists: XMen2.exe 0x0045d4c2-0x0045d55b, the conversation's
 * subtitle draw, where MSVC leaves floats on the x87 stack across intervening
 * integer pushes and hands them to helpers whose signatures the disassembly
 * does not show. Reading it gives a guess; porting a guess ships a defect;
 * refusing leaves the translated original in place forever. The fourth option
 * is to RUN it and write down what happened -- which is what this is.
 *
 * `recomp.py emit --record LO-HI` puts an X86_RECORD call before each
 * instruction in the range and nowhere else, so a build with no ranges pays
 * nothing at all and a build with them pays one predictable-branch test per
 * recorded instruction.
 *
 * WHAT A NEGATIVE PRINTS. The three answers that must not look alike are
 * "recording was never compiled in", "it was compiled in and the block never
 * executed", and "it executed and here is what it did". So the report always
 * prints, names the compiled-in ranges (the emitter registers them), and says
 * which of the three it is.
 *
 *   X2_RECORD=<path>   where to write the trace; default stderr
 *   X2_RECORD_MAX=<n>  entries to keep (default 20000); the rest are COUNTED,
 *                      never silently dropped
 *   X2_RECORD_PASSES=<n>  stop after this many passes through the region
 *                      (default 2). A per-frame block would otherwise fill the
 *                      ring with the same thing; two passes is enough to see
 *                      what varies and what does not.
 */
#include "x86rt.h"
#include "x86rt_native.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int x86_record_on = 1;      /* the macro's gate; cleared once the ring is full */

typedef struct {
    uint32_t    addr;
    const char *text;       /* a string literal in the generated code */
    uint32_t    r[8];       /* eax ecx edx ebx esp ebp esi edi */
    uint32_t    stk[8];     /* [esp .. esp+0x1c] */
    int         stk_ok;
    uint32_t    top;
    double      st[4];      /* ST(0)..ST(3) */
} Entry;

static Entry        *g_ring;
static unsigned long g_n, g_dropped, g_passes;
static unsigned long g_max = 20000;
static unsigned long g_max_passes = 2;
static uint32_t      g_first_addr;      /* the region's entry, for pass counting */
static int           g_inited;

/* The ranges the emitter compiled in. Registered by the generated module's
   constructor is the ideal; until a module does that, the report reads the
   same list the emitter printed, from the environment the build recorded. */
#define RANGES_MAX 8
static struct { uint32_t lo, hi; } g_range[RANGES_MAX];
static int g_nranges;

/*
 * Called from a constructor in EVERY generated chunk -- the header the emitter
 * writes is repeated per chunk, so this is invoked once per chunk with the
 * same ranges. Registering is therefore idempotent rather than appending,
 * which is why a twenty-chunk module does not overflow an eight-slot table
 * with twenty copies of two ranges.
 */
void x86_record_range(uint32_t lo, uint32_t hi)
{
    int i;
    for (i = 0; i < g_nranges; i++)
        if (g_range[i].lo == lo && g_range[i].hi == hi) return;
    if (g_nranges < RANGES_MAX) {
        g_range[g_nranges].lo = lo;
        g_range[g_nranges].hi = hi;
        g_nranges++;
    } else {
        fprintf(stderr, "x86_record: more than %d distinct range(s) were "
                        "emitted; the report will under-state which regions "
                        "are instrumented.\n", RANGES_MAX);
    }
}

static void record_init(void)
{
    const char *e;
    g_inited = 1;
    if ((e = getenv("X2_RECORD_MAX")) && *e) g_max = strtoul(e, NULL, 0);
    if ((e = getenv("X2_RECORD_PASSES")) && *e)
        g_max_passes = strtoul(e, NULL, 0);
    if (!g_max) g_max = 1;
    g_ring = (Entry *)calloc(g_max, sizeof *g_ring);
    if (!g_ring) {
        fprintf(stderr, "x86_record: could not allocate %lu entries; recording "
                        "is OFF and the report will say so rather than print "
                        "an empty trace.\n", g_max);
        x86_record_on = 0;
        return;
    }
    fprintf(stderr, "[REC] region recording is COMPILED IN: up to %lu entries, "
                    "%lu pass(es) through the region. Nothing outside the "
                    "emitted ranges is touched.\n", g_max, g_max_passes);
}

void x86_record(uint32_t addr, const CPU *C, const char *text)
{
    Entry *e;
    int i;

    if (!g_inited) record_init();
    if (!x86_record_on) return;

    /* Pass counting. The FIRST address ever recorded is the region's entry as
       far as this can know; seeing it again is a new pass. */
    if (!g_first_addr) g_first_addr = addr;
    else if (addr == g_first_addr) {
        g_passes++;
        if (g_passes >= g_max_passes) {
            x86_record_on = 0;
            return;
        }
    }

    if (g_n >= g_max) {
        g_dropped++;
        x86_record_on = 0;      /* keep the FIRST entries; the rest are counted */
        return;
    }

    e = &g_ring[g_n++];
    e->addr = addr;
    e->text = text;
    e->r[0] = C->eax; e->r[1] = C->ecx; e->r[2] = C->edx; e->r[3] = C->ebx;
    e->r[4] = C->esp; e->r[5] = C->ebp; e->r[6] = C->esi; e->r[7] = C->edi;
    e->top  = C->top;
    for (i = 0; i < 4; i++)
        e->st[i] = (double)C->st[(C->top + (unsigned)i) & 7];
    /* The guest stack, WITHOUT dereferencing it: a recorded block may run with
       ESP anywhere, and a diagnostic that faults is worse than one that says
       it could not look. */
    e->stk_ok = x86_peek(C->esp, e->stk, sizeof e->stk);
}

void x86_record_report(void)
{
    FILE *f = stderr;
    const char *path = getenv("X2_RECORD");
    unsigned long i;

    if (!g_nranges && !g_n && !g_inited) {
        printf("  region recording: NOT COMPILED IN -- this binary was emitted "
               "without `recomp.py emit --record LO-HI`, so no region was "
               "instrumented and nothing could have been captured.\n");
        return;
    }
    if (path && *path) {
        FILE *o = fopen(path, "w");
        if (!o) {
            fprintf(stderr, "x86_record: cannot write %s (%s); the trace goes "
                            "to stderr instead so it is not lost.\n",
                    path, strerror(errno));
        } else {
            f = o;
        }
    }

    {
        int k;
        printf("  region recording: %d range(s) compiled in:", g_nranges);
        for (k = 0; k < g_nranges; k++)
            printf(" 0x%08x-0x%08x", g_range[k].lo, g_range[k].hi);
        printf("\n");
    }
    printf("  region recording: %lu entr%s captured over %lu pass(es)"
           "%s%s\n",
           g_n, g_n == 1 ? "y" : "ies", g_passes + (g_n ? 1 : 0),
           g_dropped ? " (the ring filled; the rest were counted, not lost)"
                     : "",
           g_n ? "" : " -- the instrumented region NEVER EXECUTED in this run");
    if (!g_n) {
        if (f != stderr) fclose(f);
        return;
    }

    fprintf(f, "# region recording -- %lu entries. Columns: addr, instruction,\n"
               "# eax ecx edx ebx esp ebp esi edi, x87 top and ST(0..3), then\n"
               "# the eight dwords at [esp]. The state shown is the one the\n"
               "# instruction RAN ON, before it executed.\n", g_n);
    for (i = 0; i < g_n; i++) {
        Entry *e = &g_ring[i];
        int k;
        fprintf(f, "%08x  %-34s", e->addr, e->text ? e->text : "?");
        fprintf(f, " eax=%08x ecx=%08x edx=%08x ebx=%08x",
                e->r[0], e->r[1], e->r[2], e->r[3]);
        fprintf(f, " esp=%08x ebp=%08x esi=%08x edi=%08x",
                e->r[4], e->r[5], e->r[6], e->r[7]);
        fprintf(f, " top=%u st=[", e->top);
        for (k = 0; k < 4; k++)
            fprintf(f, "%s%.9g", k ? " " : "", e->st[k]);
        fprintf(f, "]");
        if (e->stk_ok) {
            fprintf(f, " [esp]=");
            for (k = 0; k < 8; k++)
                fprintf(f, "%s%08x", k ? "," : "", e->stk[k]);
        } else {
            fprintf(f, " [esp]=UNREADABLE");
        }
        fprintf(f, "\n");
    }
    if (g_dropped)
        fprintf(f, "# ... and %lu further instruction(s) past the ring; raise "
                   "X2_RECORD_MAX to keep them.\n", g_dropped);
    if (f != stderr) {
        printf("        the trace is in %s\n", path);
        fclose(f);
    }
}
