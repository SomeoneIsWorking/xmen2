/*
 * Native ownership of Alchemy's converted matrix state.
 *
 * libIGGfx!igDxVisualContext::setMatrix retains engine matrices, then calls
 * computeMatrix_Dx before IDirect3DDevice8::SetTransform. The internal stack
 * mapping is evidenced by DAT_100cfc60: projection 0 -> D3DTS_PROJECTION,
 * modelview 1 -> D3DTS_WORLD, and converted view 14 -> D3DTS_VIEW.
 * Mirroring the three outputs here keeps prompt vertices in the exact stock
 * (x,0,y) text plane without reconstructing intent from D3D state.
 */
#include "ui_transform.h"
#include "x2_log.h"

#include "gpu_matrix.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include "guest_body.h"
#include <stdio.h>
#include <string.h>

enum { UI_PROJECTION = 0, UI_WORLD = 1, UI_VIEW = 14 };

static float g_projection[16], g_world[16], g_view[16];
static uint32_t g_context;
static int g_have_context;
static unsigned g_valid;
static unsigned long g_calls, g_context_selections, g_captured, g_unreadable;
static unsigned long g_published, g_context_refused;

static int read_matrix(uint32_t guest, float out[16]) {
  unsigned i;
  for (i = 0; i < 16; i++) {
    uint32_t bits;
    if (!x86_peek32(guest + i * 4u, &bits))
      return 0;
    memcpy(&out[i], &bits, sizeof bits);
  }
  return 1;
}

static unsigned selector_bit(uint32_t which) {
  if (which == UI_PROJECTION)
    return 1u;
  if (which == UI_WORLD)
    return 2u;
  if (which == UI_VIEW)
    return 4u;
  return 0u;
}

static void select_context(uint32_t context) {
  if (g_have_context && g_context == context)
    return;
  g_context = context;
  g_have_context = 1;
  g_valid = 0;
  g_context_selections++;
}

void x2_ui_transform_compute_matrix(CPU *C) {
  uint32_t context = C->reg[kX86pEcx];
  uint32_t which = RD32(C->reg[kX86pEsp] + 4u);
  uint32_t output_ref = RD32(C->reg[kX86pEsp] + 8u);
  uint32_t output;
  float matrix[16];
  unsigned bit = selector_bit(which);

  g_calls++;
  /* Context and selector invalidation happen before the original body. If
     its output pointer is unreadable afterwards, no matrix retained from a
     prior call can still make this set appear complete. */
  select_context(context);
  g_valid &= ~bit;
  x86_guest_body(C, "libIGGfx.dll", 0x1003ec10u);
  if (!bit)
    return;
  if (!x86_peek32(output_ref, &output) || !read_matrix(output, matrix)) {
    g_unreadable++;
    return;
  }
  if (which == UI_PROJECTION) {
    memcpy(g_projection, matrix, sizeof matrix);
    bit = 1u;
  } else if (which == UI_WORLD) {
    memcpy(g_world, matrix, sizeof matrix);
    bit = 2u;
  } else {
    memcpy(g_view, matrix, sizeof matrix);
    bit = 4u;
  }
  g_valid |= bit;
  g_captured++;
}

__attribute__((constructor)) static void x2_ui_transform_register(void) {
  x86_register_override("libIGGfx.dll", 0x1003ec10u,
                        x2_ui_transform_compute_matrix);
}

int x2_ui_transform_current(uint32_t context, float mvp[16]) {
  float world_view[16];
  if (!mvp)
    return 0;
  if (!g_have_context || context != g_context) {
    g_context_refused++;
    return 0;
  }
  if (g_valid != 7u)
    return 0;
  gpu_matrix_multiply(g_world, g_view, world_view);
  gpu_matrix_multiply(world_view, g_projection, mvp);
  g_published++;
  return 1;
}

void x2_ui_transform_report(void) {
  x2_log_info("  Engine UI transform: %lu computeMatrix_Dx call(s), %lu "
              "visual-context cache selection(s), %lu "
              "world/view/projection capture(s), %lu "
              "unreadable; %lu complete MVP snapshot(s) published and %lu "
              "cross-context request(s) refused\n",
              g_calls, g_context_selections, g_captured, g_unreadable,
              g_published, g_context_refused);
  if (g_valid != 7u)
    x2_log_info("        incomplete matrix set (projection=%d world=%d view=%d)"
                " -- native text-plane placement is refused\n",
                !!(g_valid & 1u), !!(g_valid & 2u), !!(g_valid & 4u));
}
