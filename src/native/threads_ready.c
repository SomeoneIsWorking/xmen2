#include "threads_ready.h"

#include <math.h>

int guest_thread_ready_to_run(const GuestThread *t, double now) {
  if (!t->used || t->finished) {
    return 0;
  }
  if (t->state == TS_COND) {
    /* Woken by a broadcast, or its deadline has already passed. A wait with
       no deadline (INFINITE) carries HUGE_VAL, so only a broadcast makes it
       ready. */
    return t->cond_ready || now >= t->cond_deadline;
  }
  if (t->state == TS_SUSPENDED || t->state == TS_NEW) {
    return t->suspended == 0;
  }
  return 0;
}

int guest_thread_any_ready(const GuestThread *table, int count,
                           const GuestThread *self, double (*clock)(void)) {
  double now = 0.0;
  int have_now = 0;
  int i;
  for (i = 0; i < count; i++) {
    const GuestThread *t = &table[i];
    if (t == self) {
      continue;
    }
    if (!have_now && t->used && !t->finished && t->state == TS_COND &&
        !t->cond_ready) {
      now = clock();
      have_now = 1;
    }
    if (guest_thread_ready_to_run(t, now)) {
      return 1;
    }
  }
  return 0;
}

void guest_thread_mark_cond_ready(GuestThread *table, int count) {
  int i;
  for (i = 0; i < count; i++) {
    if (table[i].used && !table[i].finished && table[i].state == TS_COND) {
      table[i].cond_ready = 1;
    }
  }
}

void guest_thread_enter_cond_wait(GuestThread *t, uint32_t ms, double now) {
  t->cond_ready = 0;
  t->cond_deadline = (ms == 0xFFFFFFFFu) ? HUGE_VAL : now + (double)ms / 1000.0;
}

void guest_thread_leave_cond_wait(GuestThread *t) {
  t->cond_ready = 0;
  t->cond_deadline = 0.0;
}

void guest_thread_wait_deadline(const struct timespec *base, uint32_t ms,
                                struct timespec *out) {
  out->tv_sec = base->tv_sec + (time_t)(ms / 1000u);
  out->tv_nsec = base->tv_nsec + (long)(ms % 1000u) * 1000000L;
  if (out->tv_nsec >= 1000000000L) {
    out->tv_sec++;
    out->tv_nsec -= 1000000000L;
  }
}
