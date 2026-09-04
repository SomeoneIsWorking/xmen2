#include <lucent/log_c.h>
/*
 * `gfx.vtx_builder_verify` -- differential gate for
 * CDxImmediateBuilder::addVertex (XMen2.exe!0x005840a0) against the guest body.
 */
#include "vertex_builder.h"

#include "guest_body.h"
#include "x86rt.h"

#include <lucent/cvar_c.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
  SELF_CAP = 0x0cu,
  SELF_C14 = 0x14u,
  SELF_COUNT = 0x20u,
  SELF_HAS_UV = 0x24u,
  SELF_DST_POS = 0x28u,
  SELF_DST_COL = 0x2cu,
  SELF_DST_UV = 0x30u,
};

static int verify_enabled(void) {
  static int cached = -1;
  if (cached < 0)
    cached = lucent_cvar_flag("gfx.vtx_builder_verify", 0) ? 1 : 0;
  return cached;
}

void vtx_builder_verify_begin(VtxBuilderVerify *v, uint32_t self) {
  memset(v, 0, sizeof *v);
  if (!verify_enabled())
    return;

  v->active = 1;
  v->self = self;
  v->orig_count = RD32(self + SELF_COUNT);
  v->orig_dst_pos = RD32(self + SELF_DST_POS);
  v->orig_dst_col = RD32(self + SELF_DST_COL);
  v->orig_dst_uv = RD32(self + SELF_DST_UV);

  if (v->orig_dst_pos) {
    for (int i = 0; i < 12; i++)
      v->pos_before[i] = (uint8_t)RD8(v->orig_dst_pos + (uint32_t)i);
  }
  if (v->orig_dst_col) {
    for (int i = 0; i < 4; i++)
      v->col_before[i] = (uint8_t)RD8(v->orig_dst_col + (uint32_t)i);
  }
  if (v->orig_dst_uv) {
    for (int i = 0; i < 8; i++)
      v->uv_before[i] = (uint8_t)RD8(v->orig_dst_uv + (uint32_t)i);
  }
}

void vtx_builder_verify_end(const CPU *C, VtxBuilderVerify *v, uint32_t self) {
  if (!v->active)
    return;

  /* Save native outputs */
  const uint32_t nat_eax = C->reg[kX86pEax];
  const uint32_t nat_count = RD32(self + SELF_COUNT);
  const uint32_t nat_dst_pos = RD32(self + SELF_DST_POS);
  const uint32_t nat_dst_col = RD32(self + SELF_DST_COL);
  const uint32_t nat_dst_uv = RD32(self + SELF_DST_UV);

  uint8_t nat_pos[12] = {0}, nat_col[4] = {0}, nat_uv[8] = {0};
  if (v->orig_dst_pos) {
    for (int i = 0; i < 12; i++)
      nat_pos[i] = (uint8_t)RD8(v->orig_dst_pos + (uint32_t)i);
  }
  if (v->orig_dst_col) {
    for (int i = 0; i < 4; i++)
      nat_col[i] = (uint8_t)RD8(v->orig_dst_col + (uint32_t)i);
  }
  if (v->orig_dst_uv) {
    for (int i = 0; i < 8; i++)
      nat_uv[i] = (uint8_t)RD8(v->orig_dst_uv + (uint32_t)i);
  }

  /* Restore inputs */
  WR32(self + SELF_COUNT, v->orig_count);
  WR32(self + SELF_DST_POS, v->orig_dst_pos);
  WR32(self + SELF_DST_COL, v->orig_dst_col);
  WR32(self + SELF_DST_UV, v->orig_dst_uv);
  if (v->orig_dst_pos) {
    for (int i = 0; i < 12; i++)
      WR8(v->orig_dst_pos + (uint32_t)i, v->pos_before[i]);
  }
  if (v->orig_dst_col) {
    for (int i = 0; i < 4; i++)
      WR8(v->orig_dst_col + (uint32_t)i, v->col_before[i]);
  }
  if (v->orig_dst_uv) {
    for (int i = 0; i < 8; i++)
      WR8(v->orig_dst_uv + (uint32_t)i, v->uv_before[i]);
  }

  /* Run the guest body */
  CPU guest = *C;
  x86_guest_body(&guest, "XMen2.exe", 0x005840a0u);

  /* Compare results */
  int bad = 0;
  if (guest.reg[kX86pEax] != nat_eax)
    bad = 1;
  if (RD32(self + SELF_COUNT) != nat_count)
    bad = 1;
  if (RD32(self + SELF_DST_POS) != nat_dst_pos)
    bad = 1;
  if (RD32(self + SELF_DST_COL) != nat_dst_col)
    bad = 1;
  if (RD32(self + SELF_DST_UV) != nat_dst_uv)
    bad = 1;

  if (v->orig_dst_pos) {
    for (int i = 0; i < 12 && !bad; i++)
      if ((uint8_t)RD8(v->orig_dst_pos + (uint32_t)i) != nat_pos[i])
        bad = 1;
  }
  if (v->orig_dst_col) {
    for (int i = 0; i < 4 && !bad; i++)
      if ((uint8_t)RD8(v->orig_dst_col + (uint32_t)i) != nat_col[i])
        bad = 1;
  }
  if (v->orig_dst_uv) {
    for (int i = 0; i < 8 && !bad; i++)
      if ((uint8_t)RD8(v->orig_dst_uv + (uint32_t)i) != nat_uv[i])
        bad = 1;
  }

  if (bad) {
    lucent_log_error(
        "x2",
        "gfx.vtx_builder_verify: native addVertex (XMen2.exe!0x005840a0) "
        "disagrees with guest body (self 0x%08x). Aborting.\n",
        self);
    abort();
  }
}
