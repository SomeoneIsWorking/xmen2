#include "heartbeat_reports.h"

#include "../input/touch_runtime.h"
#include "control.h"
#include "d3d8_vertex_shader.h"
#include "dinput_pad.h"
#include "dinput_pad_report.h"
#include "dsound.h"
#include "guest_clock.h"
#include "kernel32_handles.h"
#include "kernel32_wait.h"
#include "movie.h"
#include "threads_yield.h"
#include "winmm.h"
#include "x86rt_native.h"

#include "x2_log.h"

void heartbeat_subsystem_reports(void) {
  /* Whatever `peek` names, on EVERY beat -- a spin is a loop over
     state the ring cannot show, and one dump at the stall shows the
     value that is stuck without showing what it was doing before. It
     prints nothing when `peek` is unset. */
  x86_peek_report();

  /* How much of this run was the scheduler asleep? Printed every beat,
     including when unbounded mode is off, so "the clock is paced" and
     "nobody wired the clock up" cannot look alike. */
  guest_clock_report();
  control_report();
  dinput_pad_poll_report();
  /* The far end of the same chain as the pad, and the only place it can be
     read on a product that never exits: a browser tab runs until it is
     closed, so the end-of-run roll-call is not reached there at all. Printed
     on every beat including the one where nothing was touched, because "no
     finger has landed yet" and "fingers landed and were dropped" are the two
     answers this feature has actually given on a device. */
  x2_touch_runtime_report("[HB] ");
  /* The audio device and the movie clock. A cutscene that never ends because
     its stream never reported itself finished is indistinguishable from a
     guest deadlock until these are on the line. */
  dsound_audio_beat_report();
  /* And the movie the audio belongs to: a title waiting for a cutscene to
     report itself finished waits without a timeout, so which side stopped --
     the guest asking or this port answering -- has to be on the line. */
  x2_movie_beat_report();
  d3d8_vs_beat_report();
}

void heartbeat_winmm_report(void) {
  /* Multimedia timers: a stall whose cause is "the callback that would have
     ended this wait never ran" looks exactly like any other stall until
     these are on the line. */
  static unsigned long p_fire, p_pump;
  unsigned long fire, pump;
  int live;
  winmm_counts(&fire, &pump, &live);
  if (fire || pump || live) {
    x2_log_error("[HB]           winmm %lu fire(s) (+%lu), "
                 "%lu pump(s) (+%lu), %d timer(s) live\n",
                 fire, fire - p_fire, pump, pump - p_pump, live);
  }
  p_fire = fire;
  p_pump = pump;
}

/*
 * The waits, next to the timers they pump: "the sleep asked for 16 ms and
 * returned after 178" and "the game ran at 2 fps" are the same statement,
 * and only these two numbers can tell them apart. HOW each park ended rides
 * with them: timed-out next to slept >> asked indicts the lock's
 * re-acquisition, signalled indicts the browser's wake path; the parks print
 * at zeroes, so silence is never a missing counter. The Sleep census names
 * the guest code doing the waiting, which is the question the totals leave.
 */
void heartbeat_wait_report(void) {
  static unsigned long p_waits, p_sig, p_tmo, p_handoff;
  static unsigned long long p_asked, p_slept;
  unsigned long waits, oversleep, sig, tmo, handoffs, handoff_worst_ms;
  unsigned long long asked, slept;
  kernel32_wait_counts(&waits, &asked, &slept, &oversleep);
  guest_yield_counts(&handoffs, &handoff_worst_ms, &sig, &tmo);
  if (waits)
    x2_log_error("[HB]           wait sleeps %lu (+%lu), asked %llu ms "
                 "(+%llu), slept %llu ms (+%llu), worst oversleep "
                 "%lu ms\n",
                 waits, waits - p_waits, asked, asked - p_asked, slept,
                 slept - p_slept, oversleep);
  x2_log_error("[HB]           parks %lu signalled (+%lu), %lu timed out "
               "(+%lu); hand-offs waited %lu (+%lu), longest %lu ms\n",
               sig, sig - p_sig, tmo, tmo - p_tmo, handoffs,
               handoffs - p_handoff, handoff_worst_ms);
  kernel32_sleep_site_report();
  p_waits = waits;
  p_asked = asked;
  p_slept = slept;
  p_sig = sig;
  p_tmo = tmo;
  p_handoff = handoffs;
}
