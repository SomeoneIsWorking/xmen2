#ifndef X2_D3D8_LIVE_RESOLUTION_H
#define X2_D3D8_LIVE_RESOLUTION_H

#include <stdint.h>

#include "d3d8_state.h"
#include "d3d8_surface.h"
#include "d3d8_types.h"

/*
 * The host has exactly one active D3D8 device. Bind its presentation state
 * after CreateDevice has made the default surfaces, and unbind it before the
 * device is torn down. The pointers remain owned by d3d8_device.c.
 */
void d3d8_live_resolution_bind(D3DPRESENT_PARAMETERS *parameters,
                               D3D8Surface *backbuffer,
                               D3D8Surface *depth,
                               D3D8State *state);
void d3d8_live_resolution_unbind(void);

/*
 * Resize the active logical backbuffer without replacing its guest-visible
 * COM objects. GetDisplayMode and the existing backbuffer/depth GetDesc calls
 * therefore agree immediately, while the host presentation and viewport are
 * reconfigured for the next frame. Refuses a resize during an open frame.
 */
int d3d8_live_resolution_apply(uint32_t width, uint32_t height,
                               char *why, int whyn);

#endif /* X2_D3D8_LIVE_RESOLUTION_H */
