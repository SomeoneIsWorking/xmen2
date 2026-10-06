/*
 * box_cull.h -- libIGSg.dll's bounding-box frustum test, as host arithmetic.
 *
 * igFrustCullNode (0x100485b0) and the box-test driver at 0x10047470 decide
 * whether a node's bounding box is visible with two leaf calls:
 *
 *   0x10047570  corners(out, min, extent, matrix)
 *     the box's eight corners, min + {0|1}*extent per axis, through a 4x4
 *     row-vector matrix into clip space: 32 floats, corner-major.
 *   0x100478e0  classify(corners)
 *     2 when every corner is behind the eye or one frustum plane has every
 *     corner outside it, 1 when every corner is inside every plane, and
 *     otherwise a finer test against a guard band: 3 when the guard-band
 *     scale is 1 or some corner is outside the band, and 5 when every corner
 *     is inside it.
 *
 * On the Dead Zone route the two were ~38% of the samples inside translated
 * code: ~250 x87 instructions and ~450 x87/integer instructions per call, each
 * paying the JIT's per-instruction x87 bookkeeping.
 *
 * EXACTNESS. The guest computes in x87 extended precision at the control word
 * the game runs with (X86P_X87_CW_INIT), keeping some intermediates in
 * registers at 64-bit precision and spilling others through 32-bit stack
 * slots. The functions below repeat the guest's own operation order and its
 * own spills, in x87_real, so on a host whose `long double` IS the x87 format
 * and whose FPU runs at that control word every result is the one the guest
 * instruction would produce (x87_exact_host()); elsewhere it is the same order
 * in doubles (x87_exact.h). classify over finite corners, the guard band and
 * the bounded verdict are exact on any host.
 */
#ifndef X2_BOX_CULL_H
#define X2_BOX_CULL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BOX_CULL_CORNERS 8u
#define BOX_CULL_CORNER_FLOATS (BOX_CULL_CORNERS * 4u)

/* classify's answer. kBoxCullUndecided is not one: it says this module
   cannot decide -- the box needs the guard-band test, or a value is one the
   host arithmetic does not repeat -- and the caller runs the guest body. */
typedef enum BoxCullVerdict {
  kBoxCullUndecided = 0,
  kBoxCullInside = 1,
  kBoxCullOutside = 2,
  kBoxCullCrossesGuardBand = 3,
  kBoxCullInsideGuardBand = 5,
} BoxCullVerdict;

/* The guard band classify tests a crossing box against: `scale` is the
   guest's float at 0x1014f4ec (.data, so the title may change it) and `one`
   its .rdata 1.0f at 0x1007bb0c, which scale is compared with. Both are
   read, not assumed. */
typedef struct BoxCullGuardBand {
  float scale;
  float one;
} BoxCullGuardBand;

/* The driver's extent, max - min per axis: the guest's fsub, spilled to a
   32-bit slot. `box` is its six floats from min.x. */
void box_cull_extent(float out[3], const float box[6]);

/* 0x10047570. `zero` is the guest's constant at 0x10077ba8 (0.0f), which the
   guest multiplies the unused extents by; it is read, not assumed. Where
   that product cannot change a corner, box_cull_corners skips it;
   box_cull_corners_guest_order always takes the guest's own steps, so a
   test can hold one against the other. */
void box_cull_corners(float out[BOX_CULL_CORNER_FLOATS], const float min[3],
                      const float extent[3], const float matrix[16],
                      float zero);
void box_cull_corners_guest_order(float out[BOX_CULL_CORNER_FLOATS],
                                  const float min[3], const float extent[3],
                                  const float matrix[16], float zero);

/* One corner's plane code: bits 2a and 2a+1 are the sign bits of the guest's
   rounded -w - v and -w + v for the corner's coordinate v on axis a, set on
   the inner side of that pair of planes. box_cull_corner_code decides finite
   corners by comparison and hands the rest to box_cull_corner_code_extended,
   which computes the guest's arithmetic; the two are exposed so a test can
   hold one against the other. */
unsigned box_cull_corner_code(const float corner[4]);
unsigned box_cull_corner_code_extended(const float corner[4]);

/* 0x100478e0, up to its guard-band test: kBoxCullUndecided for a box that
   crosses the frustum. */
BoxCullVerdict box_cull_classify(const float corners[BOX_CULL_CORNER_FLOATS]);

/*
 * The guard-band test that 0x100478e0 takes for a box classify left
 * undecided. The answer is 3 when the scale equals one. Otherwise every
 * corner's x, y and z are scaled and coded as box_cull_corner_code codes
 * them, and the answer is 5 when every scaled corner is inside every plane,
 * else 3. The guest keeps the scaled x and y in registers, where the product
 * of two floats is exact, and spills the scaled z to a float; this does the
 * same in doubles. kBoxCullUndecided when the scale or any corner is not
 * finite, or a spilled z overflows, where the guest's NaN and infinity
 * arithmetic would decide.
 */
BoxCullVerdict box_cull_guard_band(const float corners[BOX_CULL_CORNER_FLOATS],
                                   BoxCullGuardBand guard);

/* The guard band's one x87 compare, FCOMP of the scale against one, sets
   C3/C2/C0 (and clears C1) in the status word, whichever way the band then
   answers: the status word `status` becomes after it. Only a finite scale
   reaches a native answer, so the unordered case never applies. */
enum { kBoxCullGuardBandCodeMask = 0x4700 };
uint16_t box_cull_guard_band_status(uint16_t status, BoxCullGuardBand guard);

/*
 * The driver's verdict -- classify over corners -- from double arithmetic
 * with a bound on how far each double corner can lie from the guest's float
 * one: 1 with `*out` set when every sign the verdict depends on clears that
 * bound, 0 when some corner is too close to a plane (or to w = 0), when
 * `zero` is not zero, or when a value is too large for the bound to hold.
 * The caller then takes box_cull_corners and box_cull_classify. The corners
 * themselves are not produced, so this serves only a caller that keeps them
 * from the guest.
 */
int box_cull_bounded_verdict(const float min[3], const float extent[3],
                             const float matrix[16], float zero,
                             BoxCullVerdict *out);

#ifdef __cplusplus
}
#endif

#endif /* X2_BOX_CULL_H */
