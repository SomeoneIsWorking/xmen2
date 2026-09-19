#ifndef X2_THREADS_INTERNAL_H
#define X2_THREADS_INTERNAL_H

/*
 * The seam between the thread table and the things that only READ it.
 *
 * threads.c owns the records, their lifetimes and the lock that orders them.
 * The heartbeat and shutdown reports own nothing: they walk the table and
 * print it. Keeping them in the same file grew one owner past the point where
 * "what does a report see" and "who may change a record" were separable
 * questions, so the reports moved out (thread_report.c) and this header is
 * the whole of what they are allowed to touch.
 */

#include <stdint.h>

#include "platform_threads.h"

#define MAX_THREADS 16

enum {
  TS_NEW = 0,
  TS_RUNNING,
  TS_LOCK,
  TS_COND,
  TS_BLOCKING,
  TS_SUSPENDED,
  TS_DONE
};

typedef struct {
  int used, finished, suspended;
  int slot; /* index in the table; also the TLS slot */
  int is_main;
  uint32_t handle; /* the kernel32 handle the guest holds */
  uint32_t tid;
  uint32_t start, arg;
  uint32_t stack_base, stack_bytes;
  uint32_t tib;
  uint32_t exit_code;
  /* Per-thread, because the totals were misleading in exactly the way that
     matters: a run with 3,000,045 resumes and 43 suspends reads as a wildly
     active suspend/resume protocol, and is in fact one thread being resumed
     in a spin while ANOTHER sits parked and is never named. */
  unsigned long n_suspend, n_resume, n_ran;
  int reaped; /* its handle was closed and its memory freed */
  /*
   * WHAT THIS THREAD IS DOING RIGHT NOW, and since when.
   *
   * Three mechanisms were proposed for issue #57's intermittent stall and
   * all three were guesses -- a hand-off, a quantum, a lost pulse -- because
   * nothing here could answer "what is the other thread blocked ON?". The
   * totals could not: a thread parked in a condition wait and a thread
   * spinning in guest code both show up as "1 still running".
   */
  int state;
  double state_since;
  /* What it last crossed the host boundary into, and when. See
     guest_thread_note_crossing. */
  const char *last_cross;
  uint32_t last_cross_addr;
  double last_cross_at;

  pthread_t thread;
  int depth;        /* guest_lock nesting, for guest_quantum */
  int32_t priority; /* SetThreadPriority, per thread */
} GuestThread;

/* The whole table, MAX_THREADS + 1 entries; the last one is the main thread. */
GuestThread *guest_thread_table(void);
/* The calling thread's record, or NULL on a host thread that has none. */
const GuestThread *guest_thread_self_record(void);

/*
 * The scheduler's process-wide totals, copied out in one go.
 *
 * Copied rather than exported as globals so a reader cannot accidentally
 * observe half of a pair -- the suspend/resume counts are only meaningful
 * against each other.
 */
typedef struct {
  unsigned long created, exited, reaped;
  unsigned long suspends, resumes;
  unsigned long resume_noop, resume_unknown, suspend_unknown;
  unsigned long switches, quanta, quantum;
  unsigned long contended;
} GuestThreadTotals;
void guest_thread_totals(GuestThreadTotals *out);

#endif /* X2_THREADS_INTERNAL_H */
