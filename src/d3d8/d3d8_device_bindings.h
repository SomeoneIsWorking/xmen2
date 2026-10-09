#pragma once

/*
 * What the device binds -- textures, vertex streams, the index buffer -- and
 * the reference it holds on each (issue #38).
 *
 * The setters are declared rather than static because the vtable is built in
 * d3d8_device.cpp.
 */
#include <cstdint>

#include "d3d8_com.h"
#include "d3d8_state.h"

namespace x2::d3d8 {

void d3d8_dev_SetTexture(D3D8Object *self, struct X86pCpu *C);
void d3d8_dev_SetStreamSource(D3D8Object *self, struct X86pCpu *C);
void d3d8_dev_SetIndices(D3D8Object *self, struct X86pCpu *C);

/* The objects a state holds bound, taken before something replaces them
   wholesale -- a state block -- so the references can follow. */
struct D3D8BoundObjects {
  uint32_t indices;
  uint32_t stream[D3D8_MAX_STREAMS];
  uint32_t texture[D3D8_MAX_STAGES];
};

void d3d8_bound_objects_snapshot(D3D8BoundObjects *out, const D3D8State *state);

/* Move the device's references from what `before` held to what `state` now
   binds. */
void d3d8_bound_objects_follow(D3D8State *state,
                               const D3D8BoundObjects *before);

} // namespace x2::d3d8
