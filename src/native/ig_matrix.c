/* ig_matrix.c -- see ig_matrix.h. */
#include "ig_matrix.h"

typedef long double X87;

/* The four products of one element, in the order the guest adds them:
   ((p[0] + p[1]) + p[2]) + p[3]. */
static float sum4(X87 p0, X87 p1, X87 p2, X87 p3) {
  return (float)(((p0 + p1) + p2) + p3);
}

void ig_matrix44_multiply(float out[16], const float a[16], const float b[16]) {
  for (unsigned j = 0; j < 4u; j++) {
    const X87 b0 = b[j];
    const X87 b4 = b[4u + j];
    const X87 b8 = b[8u + j];
    const X87 b12 = b[12u + j];
    out[j] = sum4(a[3] * b12, a[2] * b8, a[0] * b0, a[1] * b4);
    out[4u + j] = sum4(a[7] * b12, a[5] * b4, a[6] * b8, a[4] * b0);
    out[8u + j] = sum4(a[10] * b8, a[8] * b0, a[9] * b4, a[11] * b12);
    out[12u + j] = sum4(a[15] * b12, a[13] * b4, a[14] * b8, a[12] * b0);
  }
}
