#include "heartbeat_stall.h"

#include "x2_log.h"
#include "x86rt_native.h"

/* Beats in a row with the device presenting nothing, and whether the one
   ring dump this run is allowed has been spent. */
static int g_present_stalled;
static int g_dumped;

static void dump_the_ring_once(double period, int crossings_moved) {
  if (++g_present_stalled != 2 || g_dumped) {
    return;
  }
  g_dumped = 1;
  /*
   * This is the state issue #35 was: 1051 frames at 60fps and then the frame
   * function is never entered again. Everything that could say what it is
   * doing (the ring) used to be reachable only by killing the run, and the
   * kill path is a signal handler where stdio deadlocks. From the heartbeat
   * thread it is an ordinary call.
   *
   * The guest may still be writing the ring while it is read, so an entry can
   * be torn. That is stated rather than prevented: a lock here would let a
   * diagnostic stall the run it is measuring.
   */
  x2_log_error("[HB] the device has presented nothing for %.1fs while the "
               "guest %s. Dumping the boundary ring as a snapshot -- a line "
               "may be torn. Reported once.\n",
               2 * period,
               crossings_moved ? "kept crossing the host boundary"
                               : "crossed the host boundary not once, so the "
                                 "ring's tail is the last thing it did");
  x86_ring_dump();
}

int heartbeat_stall_observe(double t, double period, unsigned long cross,
                            int crossings_moved, int have_dev,
                            int presents_moved) {
  if (have_dev && !presents_moved) {
    dump_the_ring_once(period, crossings_moved);
  } else {
    g_present_stalled = 0;
  }
  if (crossings_moved) {
    return 0;
  }
  /*
   * Three states share this one observation, and the beat cannot tell them
   * apart: it counts host-boundary crossings, and a guest spinning inside a
   * compiled block crosses nothing. Naming one of the three here was wrong on
   * a measured Android run whose JIT line reported 570,985,925 block entries,
   * 98.4% of them re-entering the block just left.
   */
  x2_log_error("[HB] %6.1fs  the guest crossed the host boundary NOT ONCE in "
               "the last %.1fs (crossings unchanged at %lu). This beat counts "
               "crossings only, so that is stopped, blocked inside host code, "
               "OR spinning in guest code that never crosses -- the engine's "
               "block-entry line below says which.\n",
               t, period, cross);
  return 1;
}

void heartbeat_stall_reset(void) {
  g_present_stalled = 0;
  g_dumped = 0;
}
