#pragma once

#include <cstdint>

#include "d3d8_com.h"

namespace x2::d3d8 {

/* Call vtable slot `slot` of `object` the way guest code does, returning EAX.
   The selftests drive the COM layer through this rather than through the
   implementation functions, so dispatch is part of what they check. */
uint32_t d3d8_selftest_call(D3D8Object *object, int slot, const uint32_t *args,
                            int nargs);

} // namespace x2::d3d8
