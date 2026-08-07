/*
 * IDirect3DDevice8 -- the object every one of the engine's ten igDx8 classes
 * ends up talking to (C113, C128), and therefore the whole point of this
 * layer.
 */
#ifndef D3D8_DEVICE_H
#define D3D8_DEVICE_H

#include <stdint.h>

#include "d3d8_com.h"
#include "d3d8_types.h"

/* Install the device's method table. Called once, from Direct3DCreate8, so
   that a build which never creates a Direct3D never builds a device vtable
   either. */
void d3d8_device_install(void);

/*
 * IDirect3D8::CreateDevice's host half. Returns NULL if the GPU device could
 * not be opened -- which the caller must turn into a failed HRESULT rather
 * than a device object that does nothing, because the engine checks.
 */
D3D8Object *d3d8_device_create(uint32_t adapter, uint32_t devtype,
                               uint32_t focus_window, uint32_t behaviour,
                               const D3DPRESENT_PARAMETERS *pp);

/* Whether the gamma ramp the engine last set would actually change the
   picture. The backend cannot programme a ramp, so this is what tells a
   silent no-op (identity) apart from a visible difference (curved). */
int d3d8_device_gamma_curved(void);

/* Frames presented, and what the engine asked for that was not there. */
void d3d8_device_report(void);

/*
 * The same counters, live, for the heartbeat (src/native/heartbeat.c).
 *
 * Returns 0 and leaves the outputs at zero when no device has ever been
 * created -- "no device" and "a device that has drawn nothing" are different
 * findings and the caller says which.
 */
int d3d8_device_counts(unsigned long *scenes, unsigned long *presents,
                       unsigned long *clears, unsigned long *draws);

#endif /* D3D8_DEVICE_H */
