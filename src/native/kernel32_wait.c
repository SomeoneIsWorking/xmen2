/* Win32 waits consume signaled objects and honor guest-clock deadlines.
 * Timer/condition-variable wakeups are opportunities to recheck, not timeouts.
 */
#include "guest_clock.h"
#include "kernel32_handles.h"
#include "threads.h"
#include "winmm.h"
#include "x2_log.h"
#include "x86rt.h"
#include "x86rt_native.h"
#include <stdlib.h>
#define A(i) RD32(C->reg[kX86pEsp] + 4u + (uint32_t)(i) * 4u)
#define WAIT_OBJECT_0 0u
#define WAIT_TIMEOUT 258u
#define WAIT_FAILED 0xFFFFFFFFu
static void ret_std(CPU *C, uint32_t value, int nargs) {
  C->reg[kX86pEax] = value;
  C->reg[kX86pEsp] += 4u + (uint32_t)nargs * 4u;
}
static uint32_t wait_remaining_ms(double start, uint32_t ms) {
  if (ms == 0xFFFFFFFFu)
    return 1000u;
  double remaining = ms - (guest_clock_now_s() - start) * 1000.0;
  if (remaining <= 0)
    return 0;
  uint32_t whole = (uint32_t)remaining;
  return whole + (remaining > whole);
}
/* Try to take one object. Returns 1 if it was signalled (and consumes it). */
static int sync_try_take(Handle *hh) {
  switch (hh->kind) {
  case H_SEM:
    if (hh->count > 0) {
      hh->count--;
      return 1;
    }
    return 0;
  case H_THREAD:
    return hh->count != 0; /* completion remains signaled for every waiter */
  case H_EVENT:
    if (hh->count) {
      if (!hh->manual)
        hh->count = 0;
      return 1;
    }
    return 0;
  case H_MUTEX:
    /* Recursive for the owner, exclusive to everyone else. */
    {
      uint32_t me = guest_current_tid();
      if (hh->count <= 0) {
        hh->owner_tid = me;
        hh->count = 1;
        return 1;
      }
      if (hh->owner_tid == me) {
        hh->count++;
        return 1;
      }
      return 0;
    }
  default:
    return 0;
  }
}

static const char *sync_kind_name(int k) {
  return k == H_SEM     ? "semaphore"
         : k == H_EVENT ? "event"
         : k == H_MUTEX ? "mutex"
                        : "non-waitable object";
}

void imp_KERNEL32_WaitForSingleObject(CPU *C) {
  Handle *hh = k32_handle_get(A(0), 0);
  uint32_t ms = A(1);
  unsigned long pulse0;
  double t0;

  if (sync_try_take(hh)) {
    ret_std(C, WAIT_OBJECT_0, 2);
    return;
  }
  if (ms == 0) {
    ret_std(C, WAIT_TIMEOUT, 2);
    return;
  }
  /*
   * A REAL wait now that guest threads exist.
   *
   * It used to abort here, and correctly: with nothing else running, no
   * signal could ever arrive and both plausible answers were lies -- success
   * hands the game a lock it does not hold, timeout claims a wait happened.
   * What changed is that something else CAN run, and the wait releases the
   * guest lock so it can (src/native/threads.c).
   *
   * Bounded even for an INFINITE wait, because "nothing will ever signal
   * this" is still possible -- one guest thread deadlocking against another
   * has to be reported, not hung on.
   */
  hh->waiters++;
  hh->n_wait++;
  pulse0 = hh->pulses;
  /*
   * The deadline is measured on the CLOCK, not counted in loop turns.
   *
   * It used to be `if (++spins == 30)`, which was 30 seconds only while each
   * turn slept a flat second. Once the sleep became "until the next timer is
   * due" a turn could be under a millisecond, and the watchdog would abort a
   * perfectly healthy wait in a few dozen of them -- a diagnostic that fires
   * on the thing it exists to rule out.
   */
  t0 = guest_clock_now_s();
  for (;;) {
    /*
     * Sleep until the next TIMER is due, not for a flat second.
     *
     * This thread is the one that will pump that timer (see below), so the
     * wait's granularity IS the timer's resolution: a flat 1000 ms slice
     * made every movie frame cost a second, and the guest sat blocked at
     * 1.3 frames per second while looking perfectly healthy.
     */
    guest_cond_wait_ms(winmm_next_due_ms(wait_remaining_ms(t0, ms)));
    /*
     * A PUMP POINT, and the one the movie player needs (issue #49).
     *
     * The multimedia timers have no thread of their own, so they run when
     * the guest next reaches a place it is not executing -- a clock read
     * or a sleep. A thread blocked HERE reaches neither, so a wait for
     * something a timer callback would produce waited forever: libCriMovie
     * sets a 1 ms timer, its decoder parks itself, and the main thread
     * waits on an event nothing can now signal. Every ingredient existed
     * and the fire never happened.
     *
     * The callback runs on THIS thread, inside the wait. Windows runs it
     * on a timer thread; that difference is the same one Sleep already
     * carries and is stated in winmm.c, not a new one introduced here.
     */
    winmm_timers_pump();
    if (sync_try_take(hh)) {
      hh->waiters--;
      ret_std(C, WAIT_OBJECT_0, 2);
      return;
    }
    /* Released by a PULSE: the object is not signalled and must not be
       taken -- being let go IS the whole event. Only a thread that was
       already waiting when the pulse happened sees the change, which is
       exactly who Win32 releases. */
    if (hh->pulses != pulse0) {
      hh->waiters--;
      ret_std(C, WAIT_OBJECT_0, 2);
      return;
    }
    if (ms != 0xFFFFFFFFu && (guest_clock_now_s() - t0) * 1000.0 >= ms) {
      hh->waiters--;
      ret_std(C, WAIT_TIMEOUT, 2);
      return;
    }
    {
      if (ms != 0xFFFFFFFFu || guest_clock_now_s() - t0 < 30.0)
        continue;
    }
    {
      x2_log_error("kernel32: WaitForSingleObject(INFINITE) on %s "
                   "\"%s\" has waited 30 seconds and nothing has "
                   "signalled it.\n"
                   "  Reporting rather than hanging: either the guest "
                   "thread that would signal it is not running, or "
                   "this host never signals that object.\n",
                   sync_kind_name(hh->kind), hh->name);
      /*
       * WHICH object, and its whole history. Issue #57 asks exactly
       * this and could not answer it: "an unnamed event" describes
       * every unnamed event in the process. The creator's return
       * address is what tells two of them apart.
       */
      x2_log_error("  handle 0x%08x, created by guest 0x%08x, %s-reset\n"
                   "  signalled %lu time(s) by SetEvent; pulsed %lu time(s), "
                   "of which %lu found NO waiter and were LOST\n"
                   "  waited on %lu time(s); %d thread(s) waiting on it now\n",
                   A(0), hh->created_by, hh->manual ? "manual" : "auto",
                   hh->n_set, hh->n_pulse_sent, hh->n_pulse_lost, hh->n_wait,
                   hh->waiters);
      if (!hh->n_set && !hh->n_pulse_sent)
        x2_log_error("  it has NEVER been signalled or pulsed, so "
                     "nothing was lost -- whatever should signal it "
                     "has not run at all.\n");
      /* And what every other guest thread is doing, which is the other
         half of a rendezvous. */
      guest_thread_state_report();
      x86_diag_dump();
      abort();
    }
  }
}

/* Take the whole set, or none of it. Split out because the "is it ready"
   question and the "take it" action must agree exactly -- a partial take
   leaves the set half-consumed and nothing can put it back. */
static int wfmo_try_all(uint32_t arr, uint32_t n) {
  uint32_t i;
  for (i = 0; i < n; i++) {
    Handle *hh = k32_handle_get(RD32(arr + i * 4u), 0);
    if (hh->kind == H_MUTEX) {
      /* Takeable if free or already ours; sync_try_take says so without
         consuming anything, because a mutex take is idempotent for the
         owner and reversible by the release below. */
      uint32_t me = guest_current_tid();
      if (hh->count > 0 && hh->owner_tid != me)
        return 0;
      continue;
    }
    if (hh->count <= 0)
      return 0;
  }
  for (i = 0; i < n; i++)
    sync_try_take(k32_handle_get(RD32(arr + i * 4u), 0));
  return 1;
}

void imp_KERNEL32_WaitForMultipleObjects(CPU *C) {
  /* (nCount, lpHandles, bWaitAll, dwMilliseconds) */
  uint32_t n = A(0), arr = A(1), all = A(2), ms = A(3), i;
  double t0;
  int warned = 0;

  if (n == 0 || n > MAX_HANDLES) {
    k32_set_last_error(87u);
    ret_std(C, WAIT_FAILED, 4);
    return;
  }
  if (!all) {
    for (i = 0; i < n; i++) {
      Handle *hh = k32_handle_get(RD32(arr + i * 4u), 0);
      if (sync_try_take(hh)) {
        ret_std(C, WAIT_OBJECT_0 + i, 4);
        return;
      }
    }
  } else if (wfmo_try_all(arr, n)) {
    ret_std(C, WAIT_OBJECT_0, 4);
    return;
  }
  if (ms == 0) {
    ret_std(C, WAIT_TIMEOUT, 4);
    return;
  }

  /*
   * A REAL wait, for the same reason WaitForSingleObject above got one: it
   * used to abort here because nothing else could run, and now something
   * can. The shape is deliberately the SAME as the single-object wait --
   * park, pump the multimedia timers on the way round (a thread blocked here
   * reaches no other pump point), re-test, and bound even an INFINITE wait
   * so a deadlock is reported rather than hung on.
   */
  for (i = 0; i < n; i++)
    k32_handle_get(RD32(arr + i * 4u), 0)->waiters++;
  t0 = guest_clock_now_s();
  for (;;) {
    guest_cond_wait_ms(winmm_next_due_ms(wait_remaining_ms(t0, ms)));
    winmm_timers_pump();
    if (all) {
      if (wfmo_try_all(arr, n)) {
        for (i = 0; i < n; i++)
          k32_handle_get(RD32(arr + i * 4u), 0)->waiters--;
        ret_std(C, WAIT_OBJECT_0, 4);
        return;
      }
    } else {
      for (i = 0; i < n; i++) {
        if (sync_try_take(k32_handle_get(RD32(arr + i * 4u), 0))) {
          uint32_t k;
          for (k = 0; k < n; k++)
            k32_handle_get(RD32(arr + k * 4u), 0)->waiters--;
          ret_std(C, WAIT_OBJECT_0 + i, 4);
          return;
        }
      }
    }
    if (ms != 0xFFFFFFFFu && (guest_clock_now_s() - t0) * 1000.0 >= ms) {
      for (i = 0; i < n; i++)
        k32_handle_get(RD32(arr + i * 4u), 0)->waiters--;
      ret_std(C, WAIT_TIMEOUT, 4);
      return;
    }
    {
      if (ms != 0xFFFFFFFFu || warned || guest_clock_now_s() - t0 < 30.0)
        continue;
    }
    /* Reported, ONCE, with every object in the set and its history -- and
       then the wait continues. An INFINITE WaitForMultipleObjects that is
       genuinely slow is not the same thing as one that will never finish,
       and aborting cannot tell them apart. The single-object watchdog
       aborts because issue #57 needed the ring at that instant; this one
       names the set and lets the run go on. */
    warned = 1;
    x2_log_error("kernel32: WaitForMultipleObjects(INFINITE, waitAll=%u) "
                 "on %u object(s) has waited 30 seconds. Each of them:\n",
                 all, n);
    for (i = 0; i < n; i++) {
      Handle *hh = k32_handle_get(RD32(arr + i * 4u), 0);
      x2_log_error("  [%u] handle 0x%08x %s \"%s\" count %d, created "
                   "by guest 0x%08x, set %lu pulsed %lu (%lu lost), "
                   "waited on %lu\n",
                   i, RD32(arr + i * 4u), sync_kind_name(hh->kind), hh->name,
                   hh->count, hh->created_by, hh->n_set, hh->n_pulse_sent,
                   hh->n_pulse_lost, hh->n_wait);
    }
    guest_thread_state_report();
  }
}
