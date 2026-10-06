#ifndef X2_TOUCH_INJECT_H
#define X2_TOUCH_INJECT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Contact phases, for the injector below. They are the SDL finger events by
   another name, kept separate so a caller needs no SDL headers. */
typedef enum {
  X2_TOUCH_PHASE_DOWN,
  X2_TOUCH_PHASE_MOTION,
  X2_TOUCH_PHASE_UP,
  X2_TOUCH_PHASE_CANCEL
} X2TouchPhase;

/*
 * Drive a contact as if the host had reported one, at a position normalized
 * to the window.
 *
 * A machine with no touchscreen cannot press an on-screen control, and a run
 * driven by a script written before it started answers whatever screen it
 * drifted onto. This goes through the SAME note-source and routing calls the
 * real host event takes, so what it exercises is the shipping path and not a
 * second copy of it. Returns what the routing returned.
 */
int x2_touch_inject(int64_t contact_id, float x, float y, X2TouchPhase phase);

#ifdef __cplusplus
}
#endif

#endif /* X2_TOUCH_INJECT_H */
