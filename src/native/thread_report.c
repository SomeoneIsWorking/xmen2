#include "x2_log.h"
/*
 * What the thread table LOOKS like from outside: the heartbeat line and the
 * shutdown tally.
 *
 * Separate from threads.c because a report reads and never writes, and because
 * the owner of the records had grown to the point where the two jobs shared
 * nothing but the array. The seam is threads_internal.h.
 */
#include "guest_clock.h"
#include "threads.h"
#include "threads_internal.h"

static const char *const TS_NAME[] = {"new",
                                      "running guest code",
                                      "runnable, waiting its turn",
                                      "in a WAIT (condition variable)",
                                      "in a blocking host call",
                                      "SUSPENDED",
                                      "finished"};

/*
 * One line per live guest thread: what it is doing and for how long.
 *
 * Printed from the heartbeat, because a stall is a thing you watch happen --
 * a report at shutdown arrives after a SIGKILL has already ended the argument.
 * The denominator is printed too: a run whose threads are all "running guest
 * code" is a different claim from a run that has no threads to report.
 */
void guest_thread_state_report(void) {
  /* A duration that keeps reading ~0.0s is not a thread that just changed
     state -- it is a thread that keeps WAKING, which is the difference
     between a poll loop and a park and is the thing worth seeing. */
  double t = guest_clock_now_s();
  GuestThread *table = guest_thread_table();
  const GuestThread *self = guest_thread_self_record();
  int i, live = 0;
  for (i = 0; i <= MAX_THREADS; i++) {
    GuestThread *g = &table[i];
    if (!g->used || g->finished)
      continue;
    live++;
    x2_log_error("[HB]           %s%u start 0x%08x: %s for %.1fs%s\n",
                 g->is_main ? "MAIN tid " : "tid ", g->tid, g->start,
                 TS_NAME[g->state < 0 || g->state > TS_DONE ? 0 : g->state],
                 t - g->state_since, g == self ? "  <- running" : "");
    /*
     * And what it last crossed into. A thread spinning inside compiled guest
     * code crosses nothing, so this is the last thing the host saw it ask
     * for -- which is the question a wedge leaves behind. Said either way:
     * "has never crossed at all" is a different finding from a stale one.
     */
    if (g->last_cross)
      x2_log_error("[HB]             last crossed into %s at guest 0x%08x, "
                   "%.1fs ago\n",
                   g->last_cross, g->last_cross_addr, t - g->last_cross_at);
    else
      x2_log_error("[HB]             has never crossed the host boundary\n");
  }
  if (!live)
    x2_log_error("[HB]           no live guest thread at all, not even "
                 "the main one -- which cannot happen while this line "
                 "is being printed, so the table is wrong\n");
}

void guest_thread_report(void) {
  GuestThread *table = guest_thread_table();
  GuestThreadTotals n;
  int i, live = 0;
  guest_thread_totals(&n);
  for (i = 0; i < MAX_THREADS; i++)
    if (table[i].used && !table[i].finished)
      live++;
  if (!n.created) {
    x2_log_info(
        "  threads: no guest thread was ever created; everything ran on "
        "the main thread.\n");
  } else {
    x2_log_info(
        "  threads: %lu created, %lu exited, %lu reaped (handle closed, "
        "stacks freed), %d still running; %lu suspend(s), %lu resume(s)\n",
        n.created, n.exited, n.reaped, live, n.suspends, n.resumes);
    if (n.resume_noop)
      x2_log_info("         %lu resume(s) were of a thread that was NOT "
                  "suspended -- Win32 no-ops those, and a loop doing them is "
                  "waiting for something else.\n",
                  n.resume_noop);
    if (n.resume_unknown || n.suspend_unknown)
      x2_log_info("         %lu resume(s) and %lu suspend(s) named NO live "
                  "thread -- a handle whose thread had already been reaped. A "
                  "loop doing that is waiting for something that cannot "
                  "happen.\n",
                  n.resume_unknown, n.suspend_unknown);
    for (i = 0; i < MAX_THREADS; i++) {
      GuestThread *t = &table[i];
      /* Reaped slots are printed too, until they are reused: their
         counters are the only record of where a spin loop's resumes
         went, and skipping them is what made 9,000,634 of them
         invisible. */
      if (!t->used && !t->tid)
        continue;
      x2_log_info("         tid %u  start 0x%08x  %lu suspend(s) %lu "
                  "resume(s) %lu turn(s)%s%s\n",
                  t->tid, t->start, t->n_suspend, t->n_resume, t->n_ran,
                  !t->used      ? "  REAPED"
                  : t->finished ? "  EXITED"
                                : "",
                  t->suspended ? "  SUSPENDED NOW -- if the run stalled, this "
                                 "is a thread waiting for a ResumeThread that "
                                 "never came"
                               : "");
    }
  }
  /* Flushed, because this now runs from the abort paths too and an unflushed
     stdout buffer is discarded by abort() -- the report was written, and
     vanished, on exactly the stall it exists to explain. */
  /*
   * Printed even when they are ZERO, with their denominators. "0
   * preemptions" and "preemption is not compiled in" are different facts and
   * a line that only appears when the number is non-zero cannot tell them
   * apart -- and zero here is itself the answer to "why did two spinning
   * threads not take turns".
   */
  x2_log_info(
      "         %lu condition/mutex hand-off(s), %lu of them preemptions (at "
      "a host crossing, or after %lu JIT step(s) without one)%s\n",
      n.switches, n.quanta, n.quantum,
      n.quanta ? ""
               : " -- NO preemption happened: either no second guest "
                 "thread was ever runnable, or the quantum is larger "
                 "than this run");
  x2_log_info("         the guest mutex was contended %lu time(s)\n",
              n.contended);
}

int guest_thread_last_crossing(const char **what, uint32_t *guest_addr,
                               double *seconds_ago) {
  const GuestThread *self = guest_thread_self_record();
  if (!self || !self->last_cross) {
    return 0;
  }
  if (what) {
    *what = self->last_cross;
  }
  if (guest_addr) {
    *guest_addr = self->last_cross_addr;
  }
  if (seconds_ago) {
    *seconds_ago = guest_clock_now_s() - self->last_cross_at;
  }
  return 1;
}
