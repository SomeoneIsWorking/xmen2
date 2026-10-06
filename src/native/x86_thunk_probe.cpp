/* See x86_thunk_probe.h. */
#include "x86_thunk_probe.h"

#include "platform_threads.h"
#include "x86rt_native.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Cumulative, written by the dispatch path and only ever read as deltas. One
   word each per thunk, no smoothing: the tight loops a hotspot is made of are
   exactly what a smoothed counter loses. */
static unsigned long g_calls[THUNK_MAX];
static unsigned long long g_ns[THUNK_MAX];

struct X86ThunkProbe {
  unsigned long calls[THUNK_MAX];
  unsigned long long ns[THUNK_MAX];
  /* Ranking scratch. Owned here rather than on the reader's stack: the
     heartbeat runs on its own thread and this is 24 KB. */
  unsigned long long key[THUNK_MAX];
  unsigned int order[THUNK_MAX];
};

unsigned long long x86_thunk_probe_clock_ns(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (unsigned long long)ts.tv_sec * 1000000000ull +
         (unsigned long long)ts.tv_nsec;
}

void x86_thunk_probe_note(uint32_t index, unsigned long long ns) {
  if (index >= THUNK_MAX)
    return;
  g_calls[index]++;
  g_ns[index] += ns;
}

X86ThunkProbe *x86_thunk_probe_create(void) {
  return calloc(1, sizeof(X86ThunkProbe));
}

void x86_thunk_probe_destroy(X86ThunkProbe *probe) { free(probe); }

/* Insert entry `index` into the descending top list, keyed on `key`. */
static unsigned int rank(unsigned int n, unsigned int cap, unsigned int index,
                         unsigned long long key, const unsigned long long *keys,
                         unsigned int *order) {
  unsigned int j;
  if (n == cap && key <= keys[order[cap - 1]])
    return n;
  if (n == cap)
    n--;
  for (j = n; j > 0 && key > keys[order[j - 1]]; j--)
    order[j] = order[j - 1];
  order[j] = index;
  return n + 1;
}

unsigned int x86_thunk_probe_top(X86ThunkProbe *probe, const char **mod,
                                 const char **sym, unsigned long *calls,
                                 unsigned long long *ns, unsigned int cap,
                                 int *by_time) {
  unsigned long long *const key = probe ? probe->key : NULL;
  unsigned int *const order = probe ? probe->order : NULL;
  unsigned int n = 0, i, count;
  int timed = 0;

  if (!probe || !cap)
    return 0;
  count = x86_thunk_count();
  if (count > THUNK_MAX)
    count = THUNK_MAX;

  for (i = 0; i < count; i++) {
    key[i] = g_ns[i] - probe->ns[i];
    if (key[i])
      timed = 1;
  }
  /* Nothing was timed, so rank on the only measurement there is. */
  if (!timed)
    for (i = 0; i < count; i++)
      key[i] = g_calls[i] - probe->calls[i];

  if (cap > count)
    cap = count;
  for (i = 0; i < count; i++)
    if (key[i])
      n = rank(n, cap, i, key[i], key, order);

  for (i = 0; i < n; i++) {
    const unsigned int t = order[i];
    const char *module = NULL;
    sym[i] = x86_thunk_name(THUNK_BASE + t * 16u, &module);
    mod[i] = module;
    calls[i] = g_calls[t] - probe->calls[t];
    ns[i] = g_ns[t] - probe->ns[t];
  }
  memcpy(probe->calls, g_calls, sizeof probe->calls);
  memcpy(probe->ns, g_ns, sizeof probe->ns);
  *by_time = timed;
  return n;
}
