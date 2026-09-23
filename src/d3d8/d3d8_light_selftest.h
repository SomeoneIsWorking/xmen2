#ifndef D3D8_LIGHT_SELFTEST_H
#define D3D8_LIGHT_SELFTEST_H

#include "d3d8_state.h"

/* Configure the production draw-path test with the highest light index
   observed in stock gameplay. */
void d3d8_light_selftest_configure(D3D8State *state);

/* Drive SetLight and LightEnable through the production device vtable. */
int d3d8_light_selftest(void);

#endif /* D3D8_LIGHT_SELFTEST_H */
