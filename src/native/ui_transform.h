/* Alchemy's current converted UI transform, before D3D8 lowering. */
#ifndef X2_UI_TRANSFORM_H
#define X2_UI_TRANSFORM_H

#include "x86rt.h"

#include <stdint.h>

/* Shipping override seam, exposed so its register/guest-memory boundary is
   exercised directly by the standalone test. */
void x2_ui_transform_compute_matrix(CPU *C);

/* Publish only the complete matrix set retained for this exact visual
   context. A matrix captured from another igDxVisualContext is never mixed. */
int x2_ui_transform_current(uint32_t context, float mvp[16]);
void x2_ui_transform_report(void);

#endif
