#ifndef X2_VERTEX_COLOR_SWIZZLE_H
#define X2_VERTEX_COLOR_SWIZZLE_H

#include "x86rt.h"

#include <stdint.h>

/*
 * libIGGfx.dll!0x10046ce0 -- the range colour-channel swap that
 * igDxVertexArray1_1 / igDx8VertexArray1_1 (shared vtable slot) run over a
 * vertex buffer whenever a colour attribute needs D3D's BGRA byte order.
 *
 * For descriptor type 2 it walks vertices [start, start+count) and, for the
 * 32-bit colour word at `base + stride*i + colour_offset`, swaps bytes 0 and 2
 * (keeps 1 and 3): out = (w & 0xff00ff00) | ((w & 0xff) << 16) | ((w >> 16) &
 * 0xff). Then it bumps the array's dirty flag (`[this+0x60]`) and lock counter
 * (`[this+0x68]`) per the call's flag argument.
 *
 * The block profile (issue #141) put the per-vertex swap loop at ~4.4% of
 * in-game guest wall time -- the single biggest block once the ADPCM and
 * _ftol2 clusters were natively owned. This override runs the swap as one
 * native call instead of a JIT block dispatch per vertex.
 */

/* The channel swap for one 32-bit colour word. */
uint32_t vtx_color_swizzle_word(uint32_t w);

/* libIGGfx.dll!0x10046ce0 -- __thiscall void(Desc *d, int flags), ret 8. */
void x2_override_10046ce0(CPU *C);

/*
 * `gfx.vtx_swizzle_verify` differential gate (vertex_color_swizzle_verify.c).
 * `begin` snapshots the pre-swap vertex span and the two dirty/lock bytes;
 * `end` restores them, re-runs the guest body, and aborts on any mismatch with
 * the native result. Both are no-ops unless the flag is set. `VtxSwizzleVerify`
 * is opaque storage the override keeps on its stack across the call.
 */
typedef struct VtxSwizzleVerify {
  uint8_t *before;
  uint32_t addr;
  uint32_t len;
  uint8_t s60;
  uint8_t s68;
  int active;
} VtxSwizzleVerify;

void vtx_swizzle_verify_begin(VtxSwizzleVerify *v, uint32_t self,
                              uint32_t desc);
void vtx_swizzle_verify_end(const CPU *C, VtxSwizzleVerify *v, uint32_t self,
                            uint32_t desc, uint32_t flags);

#endif
