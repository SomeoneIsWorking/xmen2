#ifndef DINPUT_JOYSTICK_H
#define DINPUT_JOYSTICK_H

#include "x86rt.h"

#include <stdint.h>

void dinput_joystick_state(int pad, int32_t axis_lo, int32_t axis_hi,
                           uint32_t out, uint32_t size);
uint32_t dinput_joystick_enum_objects(CPU *cpu, int pad, uint32_t callback,
                                      uint32_t context, uint32_t filter);

#endif /* DINPUT_JOYSTICK_H */
