#ifndef D3D8_LIGHT_SELFTEST_H
#define D3D8_LIGHT_SELFTEST_H

#include <stdint.h>

#include "d3d8_com.h"
#include "d3d8_state.h"

typedef uint32_t (*D3D8SelftestCall)(D3D8Object *object, int slot,
                                     const uint32_t *args, int nargs);

/* Configure the production draw-path test with the highest light index
   observed in stock gameplay. */
void d3d8_light_selftest_configure(D3D8State *state);

/* Drive SetLight and LightEnable through the production device vtable. */
int d3d8_light_selftest(D3D8SelftestCall call);

#endif /* D3D8_LIGHT_SELFTEST_H */
