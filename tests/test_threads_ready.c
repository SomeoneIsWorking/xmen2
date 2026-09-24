/*
 * When is a parked guest thread ready to take the guest lock?
 *
 * The scheduler hands its turn to "a waiter" and waits until that waiter has
 * taken one. Before this rule existed, a thread parked in an 83 ms Sleep
 * counted as a waiter, so every yield waited out the sleep and the product
 * advanced at 12 Hz. The negative cases below are the point: a sleeper with
 * time left, and an untimed wait nothing has signalled, are NOT ready.
 */
#include "threads_ready.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static unsigned checks, failures;

static void expect(int value, const char *what) {
  checks++;
  if (!value) {
    failures++;
    printf("FAIL: %s\n", what);
  }
}

static unsigned g_clock_reads;
static double g_clock_now;

static double counted_clock(void) {
  g_clock_reads++;
  return g_clock_now;
}

static GuestThread parked(double deadline, int ready) {
  GuestThread t;
  memset(&t, 0, sizeof t);
  t.used = 1;
  t.state = TS_COND;
  t.cond_deadline = deadline;
  t.cond_ready = ready;
  return t;
}

int main(void) {
  GuestThread t;

  t = parked(100.0, 0);
  expect(!guest_thread_ready_to_run(&t, 17.0),
         "a thread sleeping out a deadline is not ready");
  expect(guest_thread_ready_to_run(&t, 100.0),
         "the deadline itself makes it ready");
  expect(guest_thread_ready_to_run(&t, 101.0),
         "a deadline already passed makes it ready");

  t = parked(HUGE_VAL, 0);
  expect(!guest_thread_ready_to_run(&t, 1e9),
         "an untimed wait nothing signalled is never ready on time alone");
  t = parked(HUGE_VAL, 1);
  expect(guest_thread_ready_to_run(&t, 0.0),
         "a broadcast makes an untimed waiter ready at once");
  t = parked(100.0, 1);
  expect(guest_thread_ready_to_run(&t, 0.0),
         "a broadcast beats a deadline that has not arrived");

  memset(&t, 0, sizeof t);
  t.used = 1;
  t.state = TS_NEW;
  expect(guest_thread_ready_to_run(&t, 0.0), "a new thread is ready to start");
  t.suspended = 1;
  expect(!guest_thread_ready_to_run(&t, 0.0),
         "a thread created suspended is not ready");
  t.state = TS_SUSPENDED;
  expect(!guest_thread_ready_to_run(&t, 0.0),
         "a suspended thread is not ready");
  t.suspended = 0;
  expect(guest_thread_ready_to_run(&t, 0.0),
         "a resumed thread is ready before it runs again");

  memset(&t, 0, sizeof t);
  t.used = 1;
  t.state = TS_RUNNING;
  expect(!guest_thread_ready_to_run(&t, 0.0),
         "a running thread is not waiting for the lock");
  t.state = TS_COND;
  t.finished = 1;
  expect(!guest_thread_ready_to_run(&t, 1e9),
         "a finished thread never becomes ready");
  memset(&t, 0, sizeof t);
  expect(!guest_thread_ready_to_run(&t, 1e9), "an unused slot is not ready");

  {
    /* The scan reads the clock only for a wait whose answer needs it: none
       for a table of running and untouched slots, once however many timed
       waiters there are, and a sleeper's deadline still decides. */
    GuestThread table[4];
    memset(table, 0, sizeof table);
    table[0].used = 1;
    table[0].state = TS_RUNNING;
    g_clock_reads = 0;
    expect(!guest_thread_any_ready(table, 4, &table[0], counted_clock),
           "a thread running alone has no waiter");
    expect(g_clock_reads == 0, "a table with no condition wait reads no clock");
    table[1] = parked(100.0, 0);
    table[2] = parked(200.0, 0);
    g_clock_now = 17.0;
    expect(!guest_thread_any_ready(table, 4, &table[0], counted_clock),
           "sleepers with time left are not waiters");
    expect(g_clock_reads == 1, "the clock is read once for every sleeper");
    g_clock_now = 150.0;
    expect(guest_thread_any_ready(table, 4, &table[0], counted_clock),
           "a sleeper whose deadline passed is a waiter");
    table[1] = parked(HUGE_VAL, 1);
    table[2] = table[3];
    g_clock_reads = 0;
    expect(guest_thread_any_ready(table, 4, &table[0], counted_clock),
           "a broadcast waiter is ready");
    expect(g_clock_reads == 0, "a broadcast answers without the clock");
    expect(!guest_thread_any_ready(&table[1], 1, &table[1], counted_clock),
           "the caller's own record is never its own waiter");
  }

  {
    /* The live count decides whether the table is worth walking: the caller
       alone is not, any other live record is, and a caller that is not live
       itself -- unattached, or finished -- leaves every live record to
       another thread. */
    GuestThread self;
    memset(&self, 0, sizeof self);
    self.used = 1;
    self.state = TS_RUNNING;
    expect(!guest_thread_others_live(1, &self),
           "the caller alone has no other live thread");
    expect(guest_thread_others_live(2, &self),
           "a second live thread is worth a scan");
    expect(guest_thread_others_live(1, NULL),
           "with no calling record the one live thread is another");
    expect(!guest_thread_others_live(0, NULL), "no live thread at all");
    self.finished = 1;
    expect(guest_thread_others_live(1, &self),
           "a finished caller is not the live thread counted");
  }

  {
    struct timespec base, out;
    base.tv_sec = 10;
    base.tv_nsec = 900000000L;
    guest_thread_wait_deadline(&base, 250000u, &out);
    expect(out.tv_sec == 11 && out.tv_nsec == 150000000L,
           "a deadline that crosses a second carries into tv_sec");
    guest_thread_wait_deadline(&base, 2100000u, &out);
    expect(out.tv_sec == 13 && out.tv_nsec == 0L,
           "whole seconds and the remainder are both applied");
    guest_thread_wait_deadline(&base, 0u, &out);
    expect(out.tv_sec == 10 && out.tv_nsec == 900000000L,
           "a zero deadline is the base instant");
    base.tv_nsec = 999900000L;
    guest_thread_wait_deadline(&base, 150u, &out);
    expect(out.tv_sec == 11 && out.tv_nsec == 50000L,
           "a sub-millisecond deadline keeps its microseconds and carries");
  }

  printf("%u readiness checks, %u failures\n", checks, failures);
  return failures ? 1 : 0;
}
