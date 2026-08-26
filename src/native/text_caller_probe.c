/* TEMPORARY diagnostic: who calls the font table's getTexture?
 *
 * The glyph drawer reaches the atlas through the font table's vtable slot
 * +0x1c (FUN_005972a0), so no static call site exists to read. Overriding
 * the getter and recording its RETURN ADDRESSES answers "what draws text"
 * from a real run instead of from inference. Gated by X2_TEXTURE_PROBE so
 * ordinary runs pay nothing; super-calls the stock body either way.
 */
#include "x86rt.h"
#include "x86rt_native.h"

#include <stdio.h>
#include <stdlib.h>

#define MAX_SITES 32

void fn_XMen2_005972a0(CPU *C);

static uint32_t g_sites[MAX_SITES];
static unsigned long g_counts[MAX_SITES];
static unsigned g_n_sites;
static unsigned long g_calls;

static void note(uint32_t ret)
{
    unsigned i;
    g_calls++;
    for (i = 0; i < g_n_sites; i++)
        if (g_sites[i] == ret) { g_counts[i]++; return; }
    if (g_n_sites < MAX_SITES) {
        g_sites[g_n_sites] = ret;
        g_counts[g_n_sites] = 1;
        g_n_sites++;
        fprintf(stderr, "TEXTURE PROBE: new caller site 0x%08x (%u distinct "
                        "site(s) so far)\n", ret, g_n_sites);
    }
}

static void x2_probe_005972a0(CPU *C)
{
    note(RD32(C->esp));
    fn_XMen2_005972a0(C);
}

void x2_texture_probe_report(void)
{
    unsigned i;
    if (!getenv("X2_TEXTURE_PROBE")) return;
    fprintf(stderr, "TEXTURE PROBE: %lu getTexture call(s), %u distinct "
                    "return-site(s)", g_calls, g_n_sites);
    if (g_n_sites == MAX_SITES)
        fprintf(stderr, " (TABLE FULL -- later sites went unrecorded)");
    fprintf(stderr, "\n");
    for (i = 0; i < g_n_sites; i++)
        fprintf(stderr, "TEXTURE PROBE:   return to 0x%08x  x%lu\n",
                g_sites[i], g_counts[i]);
}

__attribute__((constructor))
static void x2_texture_probe_register(void)
{
    if (!getenv("X2_TEXTURE_PROBE")) return;
    fprintf(stderr, "TEXTURE PROBE: armed (X2_TEXTURE_PROBE); overriding "
                    "XMen2.exe getTexture 0x005972a0\n");
    x86_register_override("XMen2.exe", 0x005972a0, x2_probe_005972a0);
}
