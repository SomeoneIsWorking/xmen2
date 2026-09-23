/*
 * ig_matrix_invert.c: an inverse that multiplies back to the identity; a
 * singular matrix left untouched with C0; a determinant of exactly FLT_MIN
 * inverted with C3; a non-finite input declined. The guest-order bits are
 * proven in game by math.invert_verify.
 */
#include "ig_matrix_invert.h"

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures;

static void expect(const char *what, long got, long want) {
  if (got != want) {
    fprintf(stderr, "FAIL %s: got %ld, want %ld\n", what, got, want);
    failures++;
  }
}

static void diagonal(float m[16], float a, float b, float c, float d) {
  memset(m, 0, 16 * sizeof *m);
  m[0] = a;
  m[5] = b;
  m[10] = c;
  m[15] = d;
}

static void test_inverse(void) {
  const float m[16] = {2, 0, 0, 0, 1, 3, 0, 0, 0, 0, 4, 0, 5, 6, 7, 1};
  float inv[16];
  uint16_t codes = 0xffff;
  expect("verdict", ig_matrix44_invert(inv, m, &codes), kIgInvertInverted);
  expect("codes", codes, kIgInvertCompareGreater);
  for (unsigned r = 0; r < 4u; r++) {
    for (unsigned c = 0; c < 4u; c++) {
      double sum = 0;
      for (unsigned k = 0; k < 4u; k++) {
        sum += (double)m[4u * r + k] * inv[4u * k + c];
      }
      if (fabs(sum - (r == c)) > 1e-6) {
        fprintf(stderr, "FAIL m * inverse at %u,%u is %g\n", r, c, sum);
        failures++;
      }
    }
  }
  float self[16];
  memcpy(self, m, sizeof self);
  ig_matrix44_invert(self, self, &codes);
  expect("in place gives the same inverse", memcmp(self, inv, sizeof inv), 0);
}

static void test_singular(void) {
  float m[16];
  float out[16];
  uint16_t codes = 0;
  diagonal(m, 1, 1, 1, 0);
  memset(out, 0x5a, sizeof out);
  expect("singular", ig_matrix44_invert(out, m, &codes), kIgInvertSingular);
  expect("singular codes", codes, kIgInvertCompareLess);
  expect("singular leaves out alone", out[0] == out[1] && out[3] != 0.0f, 1);
  diagonal(m, 1, 1, 1, FLT_MIN / 2);
  expect("a denormal determinant is singular",
         ig_matrix44_invert(out, m, &codes), kIgInvertSingular);
}

static void test_boundary(void) {
  float m[16];
  float out[16];
  uint16_t codes = 0;
  diagonal(m, 1, 1, 1, FLT_MIN);
  expect("exactly FLT_MIN inverts", ig_matrix44_invert(out, m, &codes),
         kIgInvertInverted);
  expect("with C3", codes, kIgInvertCompareEqual);
}

static void test_non_finite(void) {
  float m[16];
  float out[16];
  uint16_t codes = 0;
  diagonal(m, 1, 1, 1, 1);
  m[6] = NAN;
  expect("NaN declined", ig_matrix44_invert(out, m, &codes),
         kIgInvertUndecided);
  diagonal(m, 1, 1, 1, 1);
  m[9] = INFINITY;
  expect("infinity declined", ig_matrix44_invert(out, m, &codes),
         kIgInvertUndecided);
  diagonal(m, 1e30f, 1e30f, 1e30f, 1);
  expect("an adjoint that overflows a float is declined",
         ig_matrix44_invert(out, m, &codes), kIgInvertUndecided);
}

int main(void) {
  test_inverse();
  test_singular();
  test_boundary();
  test_non_finite();
  if (failures) {
    fprintf(stderr, "%d failure(s)\n", failures);
    return 1;
  }
  printf("ig_matrix_invert: all cases passed\n");
  return 0;
}
