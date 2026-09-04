/*
 * Native stand-in for XMen2.exe!0x005840a0 -- CDxImmediateBuilder::addVertex.
 * See vertex_builder.h for the role and why (responsible for ~15% of all JIT
 * block dispatches during in-game rendering, appending vertices for text, HUD,
 * decals, particles, and immediate geometry).
 */
#include "vertex_builder.h"

#include "x86rt_native.h"

#include <stdint.h>
#include <string.h>

enum {
  SELF_CAP = 0x0cu,
  SELF_C14 = 0x14u,
  SELF_COUNT = 0x20u,
  SELF_HAS_UV = 0x24u,
  SELF_DST_POS = 0x28u,
  SELF_DST_COL = 0x2cu,
  SELF_DST_UV = 0x30u,
  SELF_STRIDE_POS = 0x48u,
  SELF_STRIDE_COL = 0x64u,
  SELF_STRIDE_UV = 0x80u,
};

void x2_override_005840a0(CPU *C) {
  const uint32_t self = C->reg[kX86pEcx];
  const uint32_t pos_ptr = RD32(C->reg[kX86pEsp] + 4u);
  const uint32_t uv_ptr = RD32(C->reg[kX86pEsp] + 8u);
  const uint32_t col = RD32(C->reg[kX86pEsp] + 12u);

  VtxBuilderVerify v;
  vtx_builder_verify_begin(&v, self);

  const uint32_t c14 = RD32(self + SELF_C14);
  const uint32_t count = RD32(self + SELF_COUNT);
  const uint32_t cap = RD32(self + SELF_CAP);

  if ((int32_t)(c14 + count + 1u) >= (int32_t)cap) {
    C->reg[kX86pEax] = count;
    vtx_builder_verify_end(C, &v, self);
    C->reg[kX86pEsp] += 16u;
    return;
  }

  WR32(self + SELF_COUNT, count + 1u);

  const uint32_t dst_pos = RD32(self + SELF_DST_POS);
  const int32_t has_uv = (int32_t)RD32(self + SELF_HAS_UV);
  const uint32_t dst_col = RD32(self + SELF_DST_COL);
  const uint32_t dst_uv = RD32(self + SELF_DST_UV);

  if (__builtin_expect(x2_write_watch_addr != 0, 0)) {
    WR32(dst_pos + 0u, RD32(pos_ptr + 0u));
    WR32(dst_pos + 4u, RD32(pos_ptr + 4u));
    WR32(dst_pos + 8u, RD32(pos_ptr + 8u));

    if (has_uv > 0) {
      WR32(dst_uv + 0u, RD32(uv_ptr + 0u));
      WR32(dst_uv + 4u, RD32(uv_ptr + 4u));
    }

    WR32(dst_col, col);

    WR32(self + SELF_DST_POS, dst_pos + RD32(self + SELF_STRIDE_POS));
    WR32(self + SELF_DST_COL, dst_col + RD32(self + SELF_STRIDE_COL));

    if (has_uv > 0) {
      const uint32_t stride_uv = RD32(self + SELF_STRIDE_UV);
      WR32(self + SELF_DST_UV, dst_uv + stride_uv);
      C->reg[kX86pEax] = stride_uv;
    } else {
      C->reg[kX86pEax] = (uint32_t)has_uv;
    }
  } else {
    /* Fast path: direct memory copies to guest addresses */
    void *h_dst_pos = x86_guest_pointer(dst_pos);
    const void *h_src_pos = x86_guest_pointer(pos_ptr);
    memcpy(h_dst_pos, h_src_pos, 12);

    if (has_uv > 0) {
      void *h_dst_uv = x86_guest_pointer(dst_uv);
      const void *h_src_uv = x86_guest_pointer(uv_ptr);
      memcpy(h_dst_uv, h_src_uv, 8);
    }

    void *h_dst_col = x86_guest_pointer(dst_col);
    memcpy(h_dst_col, &col, 4);

    WR32(self + SELF_DST_POS, dst_pos + RD32(self + SELF_STRIDE_POS));
    WR32(self + SELF_DST_COL, dst_col + RD32(self + SELF_STRIDE_COL));

    if (has_uv > 0) {
      const uint32_t stride_uv = RD32(self + SELF_STRIDE_UV);
      WR32(self + SELF_DST_UV, dst_uv + stride_uv);
      C->reg[kX86pEax] = stride_uv;
    } else {
      C->reg[kX86pEax] = (uint32_t)has_uv;
    }
  }

  vtx_builder_verify_end(C, &v, self);

  C->reg[kX86pEsp] += 16u; /* ret $0xc: pop return address and 3 dword args */
}

__attribute__((constructor)) static void vertex_builder_register(void) {
  x86_register_override("XMen2.exe", 0x005840a0u, x2_override_005840a0);
}
