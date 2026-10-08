#pragma once

#include "x86rt.h"

#include <cstdint>

namespace x2::native {

/*
 * XMen2.exe!0x005840a0 -- CDxImmediateBuilder::addVertex(pos, uv, col)
 *
 * Appends one vertex (Vec3f position, optional Vec2f UV, and 32-bit color/data)
 * into the immediate-mode geometry builder. Called ~2,600 times per frame
 * (~2.6M times per 1000 frames) for text, HUD, particle, decal, and immediate
 * geometry. In retail it calls libIGMath.dll!??4igVec3f and ??4igVec2f across
 * DLL boundaries for every vertex.
 *
 * __thiscall void(const igVec3f *pos, const igVec2f *uv, uint32_t col), ret
 * 0xc.
 */
void override_005840a0(CPU *C);
/* Its JIT leaf (override_leaf.h): the same append, declined while the
   differential gate is on. */
int vertex_builder_leaf(CPU *C);

/*
 * `gfx.vtx_builder_verify` differential gate (vertex_builder_verify.cpp).
 * When enabled, snapshots builder state before the native append, re-runs
 * the guest body, and aborts on any disagreement.
 */
struct VtxBuilderVerify {
  uint32_t self;
  uint32_t orig_count;
  uint32_t orig_dst_pos;
  uint32_t orig_dst_col;
  uint32_t orig_dst_uv;
  uint8_t pos_before[12];
  uint8_t col_before[4];
  uint8_t uv_before[8];
  int active;
};

/* Whether `gfx.vtx_builder_verify` is on: the leaf declines then, since the
   check runs the guest body. */
int vtx_builder_verifying(void);
void vtx_builder_verify_begin(VtxBuilderVerify *v, uint32_t self);
void vtx_builder_verify_end(const CPU *C, VtxBuilderVerify *v, uint32_t self);

} // namespace x2::native
