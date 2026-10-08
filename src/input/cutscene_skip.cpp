#include "cutscene_skip.h"

#include <atomic>

static std::atomic<int> g_offered;
static std::atomic<int> g_requested;
static std::atomic<unsigned long> g_accepted;
static std::atomic<unsigned long> g_refused;
static std::atomic<unsigned long> g_taken;

void x2_cutscene_skip_offer(int available) {
  g_offered.store(available != 0);
  if (!available) {
    g_requested.store(0);
  }
}

int x2_cutscene_skip_available(void) { return g_offered.load(); }

int x2_cutscene_skip_request(void) {
  if (!g_offered.load()) {
    g_refused.fetch_add(1ul);
    return 0;
  }
  g_requested.store(1);
  g_accepted.fetch_add(1ul);
  return 1;
}

int x2_cutscene_skip_take_request(void) {
  if (!g_requested.exchange(0)) {
    return 0;
  }
  g_taken.fetch_add(1ul);
  return 1;
}

X2CutsceneSkipCounts x2_cutscene_skip_counts(void) {
  X2CutsceneSkipCounts counts;
  counts.accepted = g_accepted.load();
  counts.refused = g_refused.load();
  counts.taken = g_taken.load();
  return counts;
}

void x2_cutscene_skip_reset(void) {
  g_offered.store(0);
  g_requested.store(0);
  g_accepted.store(0ul);
  g_refused.store(0ul);
  g_taken.store(0ul);
}
