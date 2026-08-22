#ifndef X2_JOYSTICK_NEUTRAL_H
#define X2_JOYSTICK_NEUTRAL_H

#include <stddef.h>
#include <stdint.h>

/* Write DirectInput's neutral DIJOYSTATE2 representation. Returns false when
   the destination cannot hold its axes, four POVs and button array. */
int x2_joystick_write_neutral(void *state, size_t size,
                              int32_t axis_lo, int32_t axis_hi);

#endif
