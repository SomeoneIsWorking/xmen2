/* Alchemy's current converted UI transform, before D3D8 lowering. */
#pragma once

#include "x86rt.h"

#include <cstdint>

namespace x2::native {

/* Shipping override seam, exposed so its register/guest-memory boundary is
   exercised directly by the standalone test. */
void ui_transform_compute_matrix(CPU *C);

/* Publish only the complete matrix set retained for this exact visual
   context. A matrix captured from another igDxVisualContext is never mixed. */
int ui_transform_current(uint32_t context, float mvp[16]);
void ui_transform_report(void);

} // namespace x2::native
