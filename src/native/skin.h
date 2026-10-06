/*
 * skin.h -- libIGMath.dll's SSE vertex-skinning loops, as host arithmetic.
 *
 *   0x10022df0  blend: every vertex through `bones` matrices, each result
 *     scaled by that bone's weight and summed.
 *   0x10022e80  rigid: every vertex through the one matrix its index names.
 *
 * A matrix is 16 floats, four rows of four; a position is (x, y, z, w) and w
 * is not read. A vertex's transform is ((row0*x + row1*y) + row2*z) + row3,
 * lane by lane, and the blend's sum starts from +0 and adds each weighted
 * transform in bone order. The output is the result's x, y and z.
 *
 * EXACTNESS. The guest computes in SSE single precision, which is IEEE binary32
 * with round-to-nearest, and x86port executes those instructions as host
 * float arithmetic in the default environment. The loops below are that same
 * arithmetic in the same order, compiled with contraction off, so each result
 * is the one the translated guest instruction produces. One difference is
 * possible and is stated rather than hidden: where both operands of one
 * multiply or add are NaNs, which payload survives is the compiler's choice
 * of operand order here and the instruction's there.
 *
 * Each vertex is read, transformed and written before the next is read, as the
 * guest does, so an output that overlaps an input sees the same values.
 */
#ifndef X2_SKIN_H
#define X2_SKIN_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SKIN_MATRIX_FLOATS 16u
#define SKIN_POSITION_FLOATS 4u

/* One vertex of 0x10022df0: `bones` (index, weight) pairs, read from
   `indices` and `weights` in order, blended into out[0..2]. */
void skin_blend_vertex(float out[3], const float position[4],
                       const uint8_t *indices, const float *weights,
                       unsigned bones, const float *matrices);

/* One vertex of 0x10022e80 through the matrix `index` names. */
void skin_rigid_vertex(float out[3], const float position[4], uint8_t index,
                       const float *matrices);

#ifdef __cplusplus
}
#endif

#endif /* X2_SKIN_H */
