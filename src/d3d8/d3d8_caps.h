/*
 * The adapter's capability block. See d3d8_caps.c for what the numbers mean
 * and why declaring one wrong is worse than declaring it small.
 */
#ifndef D3D8_CAPS_H
#define D3D8_CAPS_H

#include "d3d8_types.h"

/*
 * The few limits that come from the real GPU rather than from the profile.
 *
 * They are passed in rather than read here so this file stays free of the
 * backend: caps are a statement about the machine, and the only code that
 * knows the machine is the one that opened the device.
 */
typedef struct {
    uint32_t max_texture_dim;
    uint32_t max_volume_extent;
    uint32_t max_anisotropy;
    uint32_t max_simultaneous_textures;
} D3D8CapsLimits;

/* Conservative values for before a device exists -- GetDeviceCaps is legal on
   IDirect3D8 with no device, and the game calls it exactly there. */
void d3d8_caps_limits_default(D3D8CapsLimits *hw);

void d3d8_caps_fill(D3DCAPS8 *c, uint32_t adapter, uint32_t devtype,
                    const D3D8CapsLimits *hw);

/* The block, field by field, in the same words tools/proxy_d3d8 prints the
   REAL driver's -- so the declared profile can be diffed against the machine
   the engine was written for instead of argued about. */
void d3d8_caps_dump(const D3DCAPS8 *c, const char *who);

#endif /* D3D8_CAPS_H */
