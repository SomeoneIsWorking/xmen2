/*
 * The hot-guest-body probe: which guest entry points a slow window is spent
 * in, by wall time.
 *
 * Its own file because it is its own thing: a table, an arming switch, a
 * counter on the dispatch path and a read side, sharing no state with the
 * boundary ring or the import counters it used to sit beside.
 */
#include "x86_hotep.h"

#include <stdio.h>
#include <stdlib.h>

/*
 * Per-guest-entry-point call counts, for naming the HOT GUEST BODY behind a
 * slow window -- the level-build frames dispatch ~460k guest-to-guest calls
 * each (4x a normal frame's 110k), and the imports are not it, so the cost is
 * inside a recompiled body the ring cannot see.
 *
 * Armed by X2_HOTEP=<n> where n is the number of entry points to track: a
 * direct-mapped hash on the ENTRY POINT (key 0 = empty), so the dispatch path
 * pays one compare-and-increment against a table that never rehashes. The
 * collision check keeps each bucket honest: if a distinct EP hashes to an
 * occupied slot we record the collision once and stop counting NEW keys --
 * the table answers "which body is hot" correctly while it stays sparse,
 * which is exactly the load window. A full/overwritten table would read as
 * noise, so it refuses instead of guessing.
 *
 * The read side (x86_hotep_sorted) mirrors the thunk probe: per-interval
 * deltas, decoded to module+name by the heartbeat.
 */
#define HOTEP_MAX 4096
static unsigned g_hotep_cap;
static uint32_t g_hotep_key[HOTEP_MAX];
static unsigned long g_hotep_n[HOTEP_MAX];
static unsigned long long g_hotep_ns[HOTEP_MAX];
static unsigned g_hotep_collisions;

void x86_hotep_arm(const char *arg) {
  unsigned long want = arg ? strtoul(arg, NULL, 10) : 0;
  g_hotep_cap = want > HOTEP_MAX ? HOTEP_MAX : (unsigned)want;
  g_hotep_collisions = 0;
  if (g_hotep_cap)
    fprintf(stderr, "[HOTEP] armed for the top %u guest entry points.\n",
            g_hotep_cap);
}

void x86_hotep_count(uint32_t ep, unsigned long long ns) {
  unsigned long h;
  if (!g_hotep_cap)
    return;
  h = ((ep * 2654435761u) >> 8) % g_hotep_cap;
  if (!g_hotep_key[h]) {
    g_hotep_key[h] = ep;
    g_hotep_n[h] = 1;
    g_hotep_ns[h] = ns;
    return;
  }
  if (g_hotep_key[h] == ep) {
    g_hotep_n[h]++;
    g_hotep_ns[h] += ns;
    return;
  }
  if (g_hotep_collisions < 16)
    g_hotep_collisions++;
}

/*
 * The hot guest bodies, by ENTRY-POINT deltas since the last read.
 *
 * The heartbeat decodes each returned EP back to module+name via
 * x86_module_for / x86_native_name_at. Sorted by WALL TIME (ns), with the
 * call count alongside: the load window's ~460k dispatches/frame is the cost
 * only if the time inside them is; a cheap hot leaf tops a count sort and
 * buries the body that actually owns the 250ms. Returns 0 when unarmed.
 */
unsigned int x86_hotep_sorted(uint32_t *ep, unsigned long long *ns,
                              unsigned long *hits, unsigned int cap) {
  unsigned int n = 0;
  unsigned i;
  for (i = 0; i < g_hotep_cap && n < cap; i++) {
    unsigned long long d;
    int j;
    if (!g_hotep_key[i])
      continue;
    d = g_hotep_ns[i];
    for (j = (int)n - 1; j >= 0 && d > ns[j]; j--) {
      ep[j + 1] = ep[j];
      ns[j + 1] = ns[j];
      hits[j + 1] = hits[j];
    }
    ep[j + 1] = g_hotep_key[i];
    ns[j + 1] = d;
    hits[j + 1] = g_hotep_n[i];
    n++;
  }
  return n;
}

unsigned int x86_hotep_collisions(void) { return g_hotep_collisions; }

int x86_hotep_armed(void) { return g_hotep_cap != 0; }
