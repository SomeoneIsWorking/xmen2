/*
 * ig_matrix.h -- libIGMath.dll's igMatrix44f::multiply (0x10019520) as host
 * arithmetic.
 *
 * out = a * b for row-major 4x4 float matrices: out[4r + j] is row r of `a`
 * dotted with column j of `b`. The guest sums each element's four products in
 * x87 registers in a fixed, row-dependent order and stores the sum once:
 *
 *   row 0  ((a3 b12 + a2 b8) + a0 b0) + a1 b4
 *   row 1  ((a7 b12 + a5 b4) + a6 b8) + a4 b0
 *   row 2  ((a10 b8 + a8 b0) + a9 b4) + a11 b12
 *   row 3  ((a15 b12 + a13 b4) + a14 b8) + a12 b0
 *
 * (b's indices shown for column 0; column j adds j.) The function below
 * repeats that in `long double`, which is exact where x87_exact_host() holds.
 */
#ifndef X2_IG_MATRIX_H
#define X2_IG_MATRIX_H

#ifdef __cplusplus
extern "C" {
#endif

/* `out` must not overlap `a` or `b`. */
void ig_matrix44_multiply(float out[16], const float a[16], const float b[16]);

#ifdef __cplusplus
}
#endif

#endif /* X2_IG_MATRIX_H */
