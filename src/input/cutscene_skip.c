#include "cutscene_skip.h"

#include <stdatomic.h>

static atomic_int g_offered;
static atomic_int g_requested;
static atomic_ulong g_accepted;
static atomic_ulong g_refused;
static atomic_ulong g_taken;

void x2_cutscene_skip_offer(int available) {
  atomic_store(&g_offered, available != 0);
  if (!available) {
    atomic_store(&g_requested, 0);
  }
}

int x2_cutscene_skip_available(void) { return atomic_load(&g_offered); }

int x2_cutscene_skip_request(void) {
  if (!atomic_load(&g_offered)) {
    atomic_fetch_add(&g_refused, 1ul);
    return 0;
  }
  atomic_store(&g_requested, 1);
  atomic_fetch_add(&g_accepted, 1ul);
  return 1;
}

int x2_cutscene_skip_take_request(void) {
  if (!atomic_exchange(&g_requested, 0)) {
    return 0;
  }
  atomic_fetch_add(&g_taken, 1ul);
  return 1;
}

X2CutsceneSkipCounts x2_cutscene_skip_counts(void) {
  X2CutsceneSkipCounts counts;
  counts.accepted = atomic_load(&g_accepted);
  counts.refused = atomic_load(&g_refused);
  counts.taken = atomic_load(&g_taken);
  return counts;
}

void x2_cutscene_skip_reset(void) {
  atomic_store(&g_offered, 0);
  atomic_store(&g_requested, 0);
  atomic_store(&g_accepted, 0ul);
  atomic_store(&g_refused, 0ul);
  atomic_store(&g_taken, 0ul);
}
