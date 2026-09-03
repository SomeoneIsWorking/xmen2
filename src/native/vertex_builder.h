#ifndef X2_VERTEX_BUILDER_H
#define X2_VERTEX_BUILDER_H

#include "x86rt.h"

#include <stdint.h>

/*
 * XMen2.exe!0x005840a0 -- CDxImmediateBuilder::addVertex(pos, uv, col)
 *
 * Appends one vertex (Vec3f position, optional Vec2f UV, and 32-bit color/data)
 * into the immediate-mode geometry builder. Called ~2,600 times per frame
 * (~2.6M times per 1000 frames) for text, HUD, particle, decal, and immediate
 * geometry. In retail it calls libIGMath.dll!??4igVec3f and ??4igVec2f across
 * DLL boundaries for every vertex.
 *
 * __thiscall void(const igVec3f *pos, const igVec2f *uv, uint32_t col), ret 0xc.
 */
void x2_override_005840a0(CPU *C);

/*
 * `gfx.vtx_builder_verify` differential gate (vertex_builder_verify.c).
 * When enabled, snapshots builder state before the native append, re-runs
 * the guest body, and aborts on any disagreement.
 */
typedef struct VtxBuilderVerify {
  uint32_t self;
  uint32_t orig_count;
  uint32_t orig_dst_pos;
  uint32_t orig_dst_col;
  uint32_t orig_dst_uv;
  uint8_t pos_before[12];
  uint8_t col_before[4];
  uint8_t uv_before[8];
  int active;
} VtxBuilderVerify;

void vtx_builder_verify_begin(VtxBuilderVerify *v, uint32_t self);
void vtx_builder_verify_end(const CPU *C, VtxBuilderVerify *v, uint32_t self);

#endif
