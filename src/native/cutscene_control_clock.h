/*
 * THE CUTSCENE CLOCK -- one reader of the two guest floats that say whether a
 * cinematic still holds the player's input.
 *
 * The player read them in three places with the same four-line preamble
 * (peek the two addresses, bit-cast both, compare), and a fourth caller was
 * about to be added for the touch gate. Three copies of a predicate is three
 * chances for one of them to disagree about what "released" means, and the
 * one that disagrees is the one that leaves a movement stick over a
 * cinematic.
 *
 * A NEGATIVE deadline is the game's "no release scheduled" sentinel, not a
 * time in the past -- which is why this cannot be a plain `deadline <= now`.
 */
#ifndef X2_CUTSCENE_CONTROL_CLOCK_H
#define X2_CUTSCENE_CONTROL_CLOCK_H

#include <stdint.h>

typedef enum X2CutsceneClockState {
  /* The clock could not be read at all: no address, or unmapped guest
     memory. Distinct from "locked" on purpose -- a run that could not look
     must not be recorded as a run that looked and saw a cinematic. */
  kX2CutsceneClockUnreadable = 0,
  kX2CutsceneClockLocked,   /* Release not yet scheduled, or still ahead. */
  kX2CutsceneClockReleased, /* The deadline has passed. */
  kX2CutsceneClockCount
} X2CutsceneClockState;

const char *cutscene_control_clock_name(int state);

/* `clock` is the guest address of the cutscene clock object. */
X2CutsceneClockState cutscene_control_clock_state(uint32_t clock);

/* The raw bits of the clock's current time, for callers that compare two
   readings rather than interpreting one. 0 when unreadable. */
int cutscene_control_clock_now_bits(uint32_t clock, uint32_t *bits);

/* Schedule the release for NOW, which is what a skip does. Returns 0 when the
   clock could not be read -- the caller must not count a release it did not
   write. The deadline is the clock's, so the write lives with the reads. */
int cutscene_control_clock_release_now(uint32_t clock);

/* Guest float bits as seconds. */
float cutscene_control_clock_seconds(uint32_t bits);

#endif
