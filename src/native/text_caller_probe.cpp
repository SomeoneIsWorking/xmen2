#include "../config/environment.h"
#include "x2_log.h"
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

#include "guest_body.h"
#include <stdio.h>
#include <stdlib.h>

#define MAX_SITES 32

static uint32_t g_sites[MAX_SITES];
static unsigned long g_counts[MAX_SITES];
static unsigned g_n_sites;
static unsigned long g_calls;

static void note(uint32_t ret) {
  unsigned i;
  g_calls++;
  for (i = 0; i < g_n_sites; i++)
    if (g_sites[i] == ret) {
      g_counts[i]++;
      return;
    }
  if (g_n_sites < MAX_SITES) {
    g_sites[g_n_sites] = ret;
    g_counts[g_n_sites] = 1;
    g_n_sites++;
    x2_log_error("TEXTURE PROBE: new caller site 0x%08x (%u distinct "
                 "site(s) so far)\n",
                 ret, g_n_sites);
  }
}

static void x2_probe_005972a0(CPU *C) {
  note(RD32(C->reg[kX86pEsp]));
  x86_guest_body(C, "XMen2.exe", 0x005972a0u);
}

void x2_texture_probe_report(void) {
  unsigned i;
  if (!x2_config_override_get(kX2ConfigTextureProbe))
    return;
  x2_log_error("TEXTURE PROBE: %lu getTexture call(s), %u distinct "
               "return-site(s)",
               g_calls, g_n_sites);
  if (g_n_sites == MAX_SITES)
    x2_log_error(" (TABLE FULL -- later sites went unrecorded)");
  x2_log_error("\n");
  for (i = 0; i < g_n_sites; i++)
    x2_log_error("TEXTURE PROBE:   return to 0x%08x  x%lu\n", g_sites[i],
                 g_counts[i]);
}

__attribute__((constructor)) static void x2_texture_probe_register(void) {
  if (!x2_config_override_get(kX2ConfigTextureProbe))
    return;
  x2_log_error("TEXTURE PROBE: armed (X2_TEXTURE_PROBE); overriding "
               "XMen2.exe getTexture 0x005972a0\n");
  x86_register_override("XMen2.exe", 0x005972a0, x2_probe_005972a0);
}
