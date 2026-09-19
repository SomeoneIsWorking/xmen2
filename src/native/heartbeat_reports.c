#include "heartbeat_reports.h"

#include "../input/touch_runtime.h"
#include "control.h"
#include "dinput_pad.h"
#include "guest_clock.h"
#include "x86rt_native.h"

void heartbeat_subsystem_reports(void) {
  /* Whatever X2_PEEK names, on EVERY beat -- a spin is a loop over
     state the ring cannot show, and one dump at the stall shows the
     value that is stuck without showing what it was doing before. It
     prints nothing when X2_PEEK is unset. */
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
}
