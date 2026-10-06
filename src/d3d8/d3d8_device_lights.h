#ifndef D3D8_DEVICE_LIGHTS_H
#define D3D8_DEVICE_LIGHTS_H

/*
 * The three IDirect3DDevice8 light methods, for the device vtable.
 *
 * Declared rather than static because the vtable is built in d3d8_device.c and
 * these live beside the light diagnostics they feed.
 */
#include "d3d8_com.h"

void d3d8_dev_SetMaterial(D3D8Object *self, struct X86pCpu *C);
void d3d8_dev_SetLight(D3D8Object *self, struct X86pCpu *C);
void d3d8_dev_LightEnable(D3D8Object *self, struct X86pCpu *C);

#endif /* D3D8_DEVICE_LIGHTS_H */
