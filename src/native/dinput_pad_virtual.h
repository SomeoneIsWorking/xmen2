#ifndef X2_DINPUT_PAD_VIRTUAL_H
#define X2_DINPUT_PAD_VIRTUAL_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The SYNTHETIC gamepad: X2_VIRTUAL_PAD attaches an SDL virtual joystick so a
   headless run has a pad to find, and X2_VIRTUAL_PAD_ID gives it the
   persistent identity a stored controller0 assignment needs. Everything here
   is announced at runtime; nothing in a run using it may be mistaken for
   hardware behaviour. */

void dinput_pad_virtual_from_env(void);

/*
 * Attach the synthetic pad because TOUCH needs one, and say so.
 *
 * The on-screen controls publish through this pad -- that is what
 * dinput_pad_virtual_set/release are for -- so a run with the overlay live
 * and no pad attached routes every press into nothing. Measured in a browser
 * run: 48 contacts, 72 zone actions, 0 published, 36 refused with "this run
 * has no synthetic pad to press". The overlay drew, the zones lit up, and the
 * guest could not see any of it.
 *
 * It was only ever attached by X2_VIRTUAL_PAD (a diagnostic) or by the
 * Android bridge doing it by hand, which is why every other platform's touch
 * support was dead. The touch owner attaches its own pad instead.
 *
 * Returns nonzero when a pad is available afterwards. Idempotent: a run that
 * already has one, however it arrived, keeps it.
 */
int dinput_pad_virtual_attach_for_touch(void);
void dinput_pad_virtual_tick(unsigned long frame);
/* Button hold: 0 selects the default timed press, negative persists until
   dinput_pad_virtual_release. Axis hold 0 persists until changed/released. */
int dinput_pad_virtual_set(const char *what, double value, double hold,
                           char *why, int whyn);
int dinput_pad_virtual_release(const char *what);

/* Release WITHOUT waiting for the game to read it. For a press being taken
   back -- a lost window, a rotation, the controls turned off -- where a
   completed press is not what is being expressed. */
int dinput_pad_virtual_release_now(const char *what);

/* Releases held back until the game had read the press. Reported so "the
   press was too short to see" and "the deferral never engaged" cannot look
   alike. */
unsigned long dinput_pad_virtual_deferred_releases(void);

/* The persistent identity the synthetic pad reports, when the pad with this
   live joystick id is the synthetic one and an override was given; NULL
   otherwise. */
const char *dinput_pad_virtual_identity_override(unsigned int joystick_id);

/* The inventory slot the synthetic pad occupies, or -1 when it is not
   attached or not yet opened. Touch publishes through this pad, so the touch
   layer needs its slot to claim a player for it. */
int dinput_pad_virtual_slot(void);

/* Denominators for the shutdown report. */
void dinput_pad_virtual_counts(unsigned long *presses, unsigned long *axis_sets,
                               unsigned long *clears);

#ifdef __cplusplus
}
#endif

#endif /* X2_DINPUT_PAD_VIRTUAL_H */
