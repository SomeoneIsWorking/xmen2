/*
 * The guest lock's VOLUNTARY release, delivered as a promise rather than a
 * race.
 *
 * threads.c serializes guest execution under one mutex and yields it at the
 * quantum and at Sleep(0). The release used to be `unlock; sched_yield; lock`
 * with the lock's trylock fast path -- measured (issue #149) to hand the lock
 * back to its releaser instead of to the thread it had just woken, because
 * Emscripten's sched_yield() is explicitly not a yield on a worker and a
 * worker woken from a futex cannot win a compare-exchange against a thread
 * that is already running. Waiters therefore advanced only when the holder
 * parked for real, which is the 178-361 ms per `WaitForSingleObject` that
 * made the browser's menu phase unplayable.
 *
 * So a yield here is a promise: the releasing thread does not re-take the
 * lock until somebody else has taken a turn. The wait is bounded, because
 * the waiter count it acts on is a snapshot -- if the waiter it saw has
 * vanished, the next real acquirer or the slice expiry releases it.
 */
#include "threads_yield.h"

#include "guest_clock.h"
#include "threads.h"

#include "platform_posix.h"
#include "platform_threads.h"
#include <stdatomic.h>
#include <time.h>

#define YIELD_SLICE_MS 10

/* Any thread newly acquiring the guest lock advances this; a yielding thread
   waits for it to move under it. */
static _Atomic unsigned long g_acquires;
static _Atomic unsigned long g_yielders;
static _Atomic unsigned long g_yield_parks;
static _Atomic unsigned long g_yield_worst_ms;
static _Atomic unsigned long g_park_signalled, g_park_timed_out;
static pthread_mutex_t g_yield_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_yield_cond = PTHREAD_COND_INITIALIZER;

void guest_yield_turn(void) {
  unsigned long epoch = atomic_load_explicit(&g_acquires, memory_order_relaxed);
  int parked = 0;
  double t0 = 0.0;

  guest_unlock();
  if (atomic_load_explicit(&g_acquires, memory_order_relaxed) != epoch)
    goto relock; /* someone took a turn while we were releasing */

  pthread_mutex_lock(&g_yield_lock);
  if (scheduler_has_waiter()) {
    atomic_fetch_add(&g_yielders, 1);
    t0 = guest_clock_now_s();
    while (atomic_load_explicit(&g_acquires, memory_order_relaxed) == epoch &&
           scheduler_has_waiter()) {
      struct timespec deadline;
      clock_gettime(CLOCK_REALTIME, &deadline);
      deadline.tv_nsec += (long)YIELD_SLICE_MS * 1000000L;
      if (deadline.tv_nsec >= 1000000000L) {
        deadline.tv_sec++;
        deadline.tv_nsec -= 1000000000L;
      }
      parked = 1;
      pthread_cond_timedwait(&g_yield_cond, &g_yield_lock, &deadline);
    }
    atomic_fetch_sub(&g_yielders, 1);
  }
  pthread_mutex_unlock(&g_yield_lock);

relock:
  if (parked) {
    unsigned long ms = (unsigned long)((guest_clock_now_s() - t0) * 1000.0);
    atomic_fetch_add(&g_yield_parks, 1);
    if (ms > atomic_load(&g_yield_worst_ms))
      atomic_store(&g_yield_worst_ms, ms); /* diagnostic; racing is fine */
  }
  guest_lock();
}

void guest_yield_turn_taken(void) {
  atomic_fetch_add(&g_acquires, 1);
  if (atomic_load(&g_yielders) == 0)
    return;
  pthread_mutex_lock(&g_yield_lock);
  pthread_cond_broadcast(&g_yield_cond);
  pthread_mutex_unlock(&g_yield_lock);
}

void guest_yield_note_park(int timed_out) {
  atomic_fetch_add(timed_out ? &g_park_timed_out : &g_park_signalled, 1);
}

void guest_yield_counts(unsigned long *handoffs, unsigned long *worst_ms,
                        unsigned long *parks_signalled,
                        unsigned long *parks_timed_out) {
  if (handoffs)
    *handoffs = atomic_load(&g_yield_parks);
  if (worst_ms)
    *worst_ms = atomic_load(&g_yield_worst_ms);
  if (parks_signalled)
    *parks_signalled = atomic_load(&g_park_signalled);
  if (parks_timed_out)
    *parks_timed_out = atomic_load(&g_park_timed_out);
}
