/* The Win32 handle table behind KERNEL32; see kernel32_handles.h. */
#include "kernel32_handles.h"

#include "threads.h"
#include "x2_log.h"
#include "x86rt_native.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace x2::native {

static Handle g_h[MAX_HANDLES];

uint32_t k32_handle_alloc(int kind) {
  int i;
  for (i = 0; i < MAX_HANDLES; i++)
    if (!g_h[i].kind) {
      memset(&g_h[i], 0, sizeof g_h[i]);
      g_h[i].kind = kind;
      g_h[i].fd = -1;
      return (uint32_t)(i + 1);
    }
  x2_log_error("kernel32: more than %d handles open at once\n", MAX_HANDLES);
  abort();
}

Handle *k32_handle_get(uint32_t h, int kind) {
  if (h == 0 || h > MAX_HANDLES || !g_h[h - 1].kind) {
    x2_log_error("kernel32: handle %u is not open\n", h);
    x86_diag_dump();
    abort();
  }
  if (kind && g_h[h - 1].kind != kind) {
    x2_log_error("kernel32: handle %u is the wrong kind (%d, wanted %d)"
                 "\n",
                 h, g_h[h - 1].kind, kind);
    abort();
  }
  return &g_h[h - 1];
}

/* ---- guest threads: the handle-table half ------------------------------
 *
 * threads.cpp owns the thread; the handle table owns handles, and a thread
 * handle has to be one of these because the guest waits on it with
 * WaitForSingleObject and closes it with CloseHandle like any other.
 *
 * The thread is signalled when it EXITS, which is what Win32 means by a
 * signalled thread handle, so `count` is the same field the events and
 * semaphores use and sync_try_take works on it unchanged.
 */
uint32_t k32_handle_for_thread(void *rec) {
  uint32_t h = k32_handle_alloc(H_THREAD);
  g_h[h - 1].thread_rec = rec;
  g_h[h - 1].count = 0;  /* not signalled: still running */
  g_h[h - 1].manual = 1; /* a thread stays signalled once done */
  snprintf(g_h[h - 1].name, sizeof g_h[h - 1].name, "guest thread");
  return h;
}

void *k32_thread_record(uint32_t handle) {
  if (!handle || handle > MAX_HANDLES || g_h[handle - 1].kind != H_THREAD)
    return NULL;
  return g_h[handle - 1].thread_rec;
}

unsigned k32_thread_handle_count(void *rec) {
  unsigned i, n = 0;
  for (i = 0; i < MAX_HANDLES; ++i)
    if (g_h[i].kind == H_THREAD && g_h[i].thread_rec == rec)
      n++;
  return n;
}

void k32_handle_thread_done(void *rec) {
  unsigned i;
  for (i = 0; i < MAX_HANDLES; ++i)
    if (g_h[i].kind == H_THREAD && g_h[i].thread_rec == rec)
      g_h[i].count = 1; /* every alias stays signalled */
}

/* Proves the shipping handle table preserves thread OBJECT identity across
   numeric aliases. This is the exact operation libCriMovie uses for its
   self-suspend/resume handle; testing only the original handle missed #57. */
int kernel32_thread_alias_selftest(void) {
  void *rec = guest_thread_current_record();
  uint32_t original = k32_handle_for_thread(rec);
  uint32_t alias = k32_handle_alloc(H_THREAD);
  int fails = 0;

  g_h[alias - 1] = g_h[original - 1];
  if (k32_thread_record(original) != rec || k32_thread_record(alias) != rec ||
      !guest_thread_is_thread(alias) || guest_thread_resume(alias) < 0) {
    x2_log_info("kernel32 thread-alias selftest: FAILED -- duplicated handle "
                "%u did not control the same thread as %u.\n",
                alias, original);
    fails++;
  }
  guest_thread_handle_closed(alias);
  g_h[alias - 1].kind = 0;
  if (!guest_thread_is_thread(original)) {
    x2_log_info("kernel32 thread-alias selftest: FAILED -- closing one alias "
                "detached the still-open original.\n");
    fails++;
  }
  guest_thread_handle_closed(original);
  g_h[original - 1].kind = 0;
  x2_log_info("kernel32 thread-alias selftest: %s -- two numeric handles %s "
              "one guest thread object\n",
              fails ? "FAILED" : "PASSED",
              fails ? "did not preserve" : "preserved");
  return fails;
}

} // namespace x2::native
