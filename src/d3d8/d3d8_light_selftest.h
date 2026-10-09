#pragma once

#include "d3d8_state.h"

namespace x2::d3d8 {

/* Configure the production draw-path test with the highest light index
   observed in stock gameplay. */
void d3d8_light_selftest_configure(D3D8State *state);

/* Drive SetLight and LightEnable through the production device vtable. */
int d3d8_light_selftest(void);

} // namespace x2::d3d8
