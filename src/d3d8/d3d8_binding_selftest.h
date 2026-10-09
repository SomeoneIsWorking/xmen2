#pragma once

namespace x2::d3d8 {

/* Drive SetStreamSource, SetIndices and SetTexture through the production
   device vtable and check the references the device holds on what it binds. */
int d3d8_binding_selftest(void);

} // namespace x2::d3d8
