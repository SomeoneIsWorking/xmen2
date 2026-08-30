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
void dinput_pad_virtual_tick(unsigned long frame);
/* Button hold: 0 selects the default timed press, negative persists until
   dinput_pad_virtual_release. Axis hold 0 persists until changed/released. */
int dinput_pad_virtual_set(const char *what, double value, double hold,
                           char *why, int whyn);
int dinput_pad_virtual_release(const char *what);

/* The persistent identity the synthetic pad reports, when the pad with this
   live joystick id is the synthetic one and an override was given; NULL
   otherwise. */
const char *dinput_pad_virtual_identity_override(unsigned int joystick_id);

/* Denominators for the shutdown report. */
void dinput_pad_virtual_counts(unsigned long *presses, unsigned long *axis_sets,
                               unsigned long *clears);

#ifdef __cplusplus
}
#endif

#endif /* X2_DINPUT_PAD_VIRTUAL_H */
