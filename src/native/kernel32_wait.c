/* Win32 waits consume signaled objects and honor guest-clock deadlines.
 * Timer/condition-variable wakeups are opportunities to recheck, not timeouts.
 */
#include "kernel32_wait.h"
#include "guest_clock.h"
#include "kernel32_handles.h"
#include "stdcall_import.h"
#include "threads.h"
#include "winmm.h"
#include "x2_log.h"
#include "x86rt.h"
#include "x86rt_native.h"
#include <stdlib.h>
#define WAIT_OBJECT_0 0u
#define WAIT_TIMEOUT 258u
#define WAIT_FAILED 0xFFFFFFFFu
static uint32_t wait_remaining_ms(double start, uint32_t ms) {
  if (ms == 0xFFFFFFFFu)
    return 1000u;
  double remaining = ms - (guest_clock_now_s() - start) * 1000.0;
  if (remaining <= 0)
    return 0;
  uint32_t whole = (uint32_t)remaining;
  return whole + (remaining > whole);
}

/*
 * What the blocking waits asked the scheduler for, and what they got.
 *
 * A sleep that comes back long is the difference between "the guest is slow"
 * and "the guest is late", and the guest's multimedia timers have no thread of
 * their own -- the waiting thread fires the callback that ends the wait (see
 * winmm.c) -- so this is also the guest timer's real resolution. Reported in
 * the heartbeat, with a denominator, because a run ends by timeout and a
 * number that only prints at shutdown cannot measure this program.
 */
static unsigned long g_wait_sleeps;
static unsigned long long g_wait_asked_ms, g_wait_slept_ms;
static unsigned long g_wait_worst_oversleep_ms;

static void wait_note(uint32_t asked_ms, uint32_t slept_ms) {
  g_wait_sleeps++;
  g_wait_asked_ms += asked_ms;
  g_wait_slept_ms += slept_ms;
  if (slept_ms > asked_ms && slept_ms - asked_ms > g_wait_worst_oversleep_ms)
    g_wait_worst_oversleep_ms = slept_ms - asked_ms;
}

void kernel32_wait_counts(unsigned long *sleeps, unsigned long long *asked_ms,
                          unsigned long long *slept_ms,
                          unsigned long *worst_oversleep_ms) {
  if (sleeps)
    *sleeps = g_wait_sleeps;
  if (asked_ms)
    *asked_ms = g_wait_asked_ms;
  if (slept_ms)
    *slept_ms = g_wait_slept_ms;
  if (worst_oversleep_ms)
    *worst_oversleep_ms = g_wait_worst_oversleep_ms;
}

/*
 * WHO is sleeping, not just how much.
 *
 * A stalled run whose wall clock is 99% Sleep has one question left: which
 * guest code is in the wait loop. The return address on entry answers it, so
 * a small census of call sites carries the answer into the heartbeat. Bounded
 * on purpose, and a site that does not fit is COUNTED rather than dropped in
 * silence -- a census that can quietly lose the hot site is not evidence.
 */
#define SLEEP_SITES 8

static struct {
  uint32_t site;
  unsigned long calls;
  unsigned long long asked_ms;
} g_sleep_site[SLEEP_SITES];
static unsigned long g_sleep_sites_refused;

static void sleep_site_note(uint32_t site, uint32_t ms) {
  int i;
  for (i = 0; i < SLEEP_SITES; ++i) {
    if (g_sleep_site[i].calls != 0u && g_sleep_site[i].site != site) {
      continue;
    }
    if (g_sleep_site[i].calls == 0u) {
      g_sleep_site[i].site = site;
    }
    g_sleep_site[i].calls++;
    g_sleep_site[i].asked_ms += ms;
    return;
  }
  g_sleep_sites_refused++;
}

void kernel32_sleep_site_report(void) {
  int i;
  int printed = 0;
  for (i = 0; i < SLEEP_SITES; ++i) {
    if (g_sleep_site[i].calls == 0u) {
      continue;
    }
    printed++;
    x2_log_error("[HB]             Sleep from guest 0x%08x: %lu call(s), "
                 "%llu ms asked\n",
                 g_sleep_site[i].site, g_sleep_site[i].calls,
                 g_sleep_site[i].asked_ms);
  }
  if (printed == 0) {
    x2_log_error("[HB]             no guest code has called Sleep at all\n");
  }
  if (g_sleep_sites_refused != 0u) {
    x2_log_error("[HB]             %lu Sleep call(s) came from a site beyond "
                 "the %d this census holds, so the ranking above may miss "
                 "the hot one\n",
                 g_sleep_sites_refused, SLEEP_SITES);
  }
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
    {
      uint32_t asked = winmm_next_due_ms(wait_remaining_ms(t0, ms));
      double slept_at = guest_clock_now_s();
      guest_cond_wait_ms(asked);
      wait_note(asked, (uint32_t)((guest_clock_now_s() - slept_at) * 1000.0));
    }
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
    {
      uint32_t asked = winmm_next_due_ms(wait_remaining_ms(t0, ms));
      double slept_at = guest_clock_now_s();
      guest_cond_wait_ms(asked);
      wait_note(asked, (uint32_t)((guest_clock_now_s() - slept_at) * 1000.0));
    }
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

/*
 * Sleep.
 *
 * It lives with the other blocking waits because it IS one, and because the
 * counters above are what the heartbeat reports: measured in the browser, a
 * stalled retail boot spent 4,948 ms of every 5,000 in this call, and the
 * wait line beside it read "+0" because Sleep was not counted anywhere. A
 * sleep report that cannot see the dominant sleeper is worse than none.
 */
void imp_KERNEL32_Sleep(CPU *C) {
  uint32_t ms = A(0);
  double began = guest_clock_now_s();
  /* Through the scheduler, not usleep: a usleep here stopped every guest
     thread for the duration -- including whichever one this sleep is
     waiting for. */
  sleep_site_note(RD32(C->reg[kX86pEsp]), ms);
  guest_sleep_ms(ms);
  wait_note(ms, (uint32_t)((guest_clock_now_s() - began) * 1000.0 + 0.5));
  /* The other pump point, and the one that matters most: a guest that sleeps
     waiting for a timer callback would otherwise sleep forever. Pumped
     AFTER the sleep, so a callback due during it fires as soon as it can. */
  winmm_timers_pump();
  ret_std(C, 0, 1);
}
