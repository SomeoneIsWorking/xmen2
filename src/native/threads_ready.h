#ifndef X2_THREADS_READY_H
#define X2_THREADS_READY_H

#include "threads_internal.h"

#include <time.h>

/*
 * Is this thread able to take the guest lock NOW?
 *
 * The scheduler's hand-off promise (threads_yield.c) makes a yielding thread
 * wait until somebody else has taken a turn. That is only sound when somebody
 * else CAN: a thread parked in an 83 ms Sleep is in a condition wait, and
 * treating it as a candidate made every quantum yield wait out its deadline,
 * which held the browser's frame rate at the sleeper's 12 Hz.
 *
 * `now` is the guest clock, passed in so the rule is a pure function of the
 * record and the time -- the same rule the scheduler runs, testable without
 * a scheduler.
 */
int guest_thread_ready_to_run(const GuestThread *t, double now);

/*
 * Entering and leaving a condition wait, as far as readiness is concerned:
 * `ms` is the Win32 deadline, 0xFFFFFFFF for INFINITE, and `now` the guest
 * clock. The pair keeps the deadline and the broadcast flag in one place
 * rather than spread through the scheduler.
 */
void guest_thread_enter_cond_wait(GuestThread *t, uint32_t ms, double now);
void guest_thread_leave_cond_wait(GuestThread *t);

/* The CLOCK_REALTIME deadline `ms` from `base` names, normalised. Split out
   so the nanosecond carry is exercised rather than trusted. */
void guest_thread_wait_deadline(const struct timespec *base, uint32_t ms,
                                struct timespec *out);

/*
 * A broadcast has gone out: every thread parked in a condition wait will
 * leave it as soon as it can take the lock, whatever its deadline said.
 * `count` is the number of records in `table`.
 */
void guest_thread_mark_cond_ready(GuestThread *table, int count);

#endif
