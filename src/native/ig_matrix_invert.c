/* ig_matrix_invert.c -- see ig_matrix_invert.h.
 *
 * adjoint() and determinant() are the guest bodies statement for statement:
 * every `(float)` is a 32-bit spill the guest makes, every other value stays
 * in an x87 register. They were generated from the disassembly by a symbolic
 * x87 evaluation and checked against m * adj(m) = det(m) * I.
 */
#include "ig_matrix_invert.h"
#include "x87_exact.h"

#include <float.h>
#include <math.h>

typedef x87_real X87;

/* libIGMath 0x1001b5e0, igMatrix44f::adjoint(const igMatrix44f &m,
   igMatrix44f &adj): the transposed cofactors. */
static void adjoint(const float m[16], float adj[16]) {
  const float t0 = (float)(((X87)m[15] * m[10]) - ((X87)m[14] * m[11]));
  const float t1 = (float)(((X87)m[15] * m[9]) - ((X87)m[13] * m[11]));
  const float t2 = (float)(((X87)m[14] * m[9]) - ((X87)m[13] * m[10]));
  const float t3 =
      (float)((((X87)t0 * (X87)m[5]) - ((X87)t1 * m[6])) + ((X87)t2 * m[7]));
  adj[0] = t3;
  const float t4 = (float)(((X87)m[15] * m[8]) - ((X87)m[12] * m[11]));
  const float t5 = (float)(((X87)m[14] * m[8]) - ((X87)m[12] * m[10]));
  const float t6 =
      (float)(-((((X87)t0 * (X87)m[4]) - ((X87)t4 * m[6])) + ((X87)t5 * m[7])));
  adj[4] = t6;
  const float t7 = (float)(((X87)m[13] * m[8]) - ((X87)m[12] * m[9]));
  const float t8 = (float)((((X87)t1 * (X87)m[4]) - ((X87)t4 * (X87)m[5])) +
                           ((X87)t7 * m[7]));
  adj[8] = t8;
  const float t9 = (float)(-((((X87)t2 * (X87)m[4]) - ((X87)t5 * (X87)m[5])) +
                             ((X87)t7 * m[6])));
  adj[12] = t9;
  const float t10 = (float)(-((((X87)t0 * (X87)m[1]) - ((X87)t1 * (X87)m[2])) +
                              ((X87)t2 * (X87)m[3])));
  adj[1] = t10;
  const float t11 = (float)((((X87)t0 * (X87)m[0]) - ((X87)t4 * (X87)m[2])) +
                            ((X87)t5 * (X87)m[3]));
  adj[5] = t11;
  const float t12 = (float)(-((((X87)t1 * (X87)m[0]) - ((X87)t4 * (X87)m[1])) +
                              ((X87)t7 * (X87)m[3])));
  adj[9] = t12;
  const float t13 = (float)((((X87)t2 * (X87)m[0]) - ((X87)t5 * (X87)m[1])) +
                            ((X87)t7 * (X87)m[2]));
  adj[13] = t13;
  const float t14 = (float)(((X87)m[15] * m[6]) - ((X87)m[14] * m[7]));
  const float t15 = (float)(((X87)m[15] * (X87)m[5]) - ((X87)m[13] * m[7]));
  const float t16 = (float)(((X87)m[14] * (X87)m[5]) - ((X87)m[13] * m[6]));
  const float t17 = (float)((((X87)t14 * (X87)m[1]) - ((X87)t15 * (X87)m[2])) +
                            ((X87)t16 * (X87)m[3]));
  adj[2] = t17;
  const float t18 = (float)(((X87)m[15] * (X87)m[4]) - ((X87)m[12] * m[7]));
  const float t19 = (float)(((X87)m[14] * (X87)m[4]) - ((X87)m[12] * m[6]));
  const float t20 =
      (float)(-((((X87)t14 * (X87)m[0]) - ((X87)t18 * (X87)m[2])) +
                ((X87)t19 * (X87)m[3])));
  adj[6] = t20;
  const float t21 =
      (float)(((X87)m[13] * (X87)m[4]) - ((X87)m[12] * (X87)m[5]));
  const float t22 = (float)((((X87)t15 * (X87)m[0]) - ((X87)t18 * (X87)m[1])) +
                            ((X87)t21 * (X87)m[3]));
  adj[10] = t22;
  const float t23 =
      (float)(-((((X87)t16 * (X87)m[0]) - ((X87)t19 * (X87)m[1])) +
                ((X87)t21 * (X87)m[2])));
  adj[14] = t23;
  const float t24 = (float)(((X87)m[11] * m[6]) - ((X87)m[10] * m[7]));
  const float t25 = (float)(((X87)m[11] * (X87)m[5]) - ((X87)m[9] * m[7]));
  const float t26 = (float)(((X87)m[10] * (X87)m[5]) - ((X87)m[9] * m[6]));
  const float t27 =
      (float)(-((((X87)t24 * (X87)m[1]) - ((X87)t25 * (X87)m[2])) +
                ((X87)t26 * (X87)m[3])));
  adj[3] = t27;
  const float t28 = (float)(((X87)m[11] * (X87)m[4]) - ((X87)m[8] * m[7]));
  const float t29 = (float)(((X87)m[10] * (X87)m[4]) - ((X87)m[8] * m[6]));
  const float t30 = (float)((((X87)t24 * (X87)m[0]) - ((X87)t28 * (X87)m[2])) +
                            ((X87)t29 * (X87)m[3]));
  adj[7] = t30;
  const float t31 = (float)(((X87)m[9] * (X87)m[4]) - ((X87)m[8] * (X87)m[5]));
  const float t32 =
      (float)(-((((X87)t25 * (X87)m[0]) - ((X87)t28 * (X87)m[1])) +
                ((X87)t31 * (X87)m[3])));
  adj[11] = t32;
  const float t33 = (float)((((X87)t26 * (X87)m[0]) - ((X87)t29 * (X87)m[1])) +
                            ((X87)t31 * (X87)m[2]));
  adj[15] = t33;
}

/* libIGMath 0x1001b980, igMatrix44f::determinant(const igMatrix44f &m): the
   expansion along row 0 over 2x2 minors of rows 2 and 3, left in ST(0). */
static X87 determinant(const float m[16]) {
  const float t0 = (float)(((X87)m[15] * m[10]) - ((X87)m[14] * m[11]));
  const float t1 = (float)(((X87)m[15] * (X87)m[9]) - ((X87)m[13] * m[11]));
  const float t2 = (float)(((X87)m[14] * (X87)m[9]) - ((X87)m[13] * m[10]));
  const float t3 = (float)(((X87)m[15] * (X87)m[8]) - ((X87)m[12] * m[11]));
  const float t4 = (float)(((X87)m[14] * (X87)m[8]) - ((X87)m[12] * m[10]));
  const float t5 = (float)(((X87)m[13] * (X87)m[8]) - ((X87)m[12] * (X87)m[9]));
  return ((((((((X87)t0 * (X87)m[5]) - ((X87)t1 * (X87)m[6])) +
              ((X87)t2 * (X87)m[7])) *
             m[0]) -
            (((((X87)t0 * (X87)m[4]) - ((X87)t3 * (X87)m[6])) +
              ((X87)t4 * (X87)m[7])) *
             m[1])) +
           (((((X87)t1 * (X87)m[4]) - ((X87)t3 * (X87)m[5])) +
             ((X87)t5 * (X87)m[7])) *
            m[2])) -
          (((((X87)t2 * (X87)m[4]) - ((X87)t4 * (X87)m[5])) +
            ((X87)t5 * (X87)m[6])) *
           m[3]));
}

static int all_finite(const float v[16]) {
  for (unsigned i = 0; i < 16u; i++) {
    if (!isfinite(v[i])) {
      return 0;
    }
  }
  return 1;
}

IgInvertVerdict ig_matrix44_invert(float out[16], const float m[16],
                                   uint16_t *compare_codes) {
  float adj[16];
  if (!all_finite(m)) {
    return kIgInvertUndecided;
  }
  adjoint(m, adj);
  if (!all_finite(adj)) {
    return kIgInvertUndecided;
  }
  const X87 det = determinant(m);
  const float magnitude = fabsf((float)det);
  if (magnitude < FLT_MIN) {
    *compare_codes = kIgInvertCompareLess;
    return kIgInvertSingular;
  }
  *compare_codes =
      magnitude == FLT_MIN ? kIgInvertCompareEqual : kIgInvertCompareGreater;
  const X87 scale = 1.0L / det;
  float inverse[16];
  for (unsigned i = 0; i < 16u; i++) {
    inverse[i] = (float)(scale * adj[i]);
  }
  if (!all_finite(inverse)) {
    return kIgInvertUndecided;
  }
  for (unsigned i = 0; i < 16u; i++) {
    out[i] = inverse[i];
  }
  return kIgInvertInverted;
}
