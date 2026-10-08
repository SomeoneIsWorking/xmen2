#pragma once

#include <cstddef>
#include <cstdint>

namespace x2::input {

/* Write DirectInput's neutral DIJOYSTATE2 representation. Returns false when
   the destination cannot hold its axes, four POVs and button array. */
int joystick_write_neutral(void *state, size_t size, int32_t axis_lo,
                           int32_t axis_hi);

} // namespace x2::input
