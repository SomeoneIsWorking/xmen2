/*
 * skin.c against hand-worked cases. Each discriminator is a sum whose value
 * depends on the order the guest adds in, on a separate rounding of each
 * product, or on the sign a +0 accumulator gives -0: an implementation that
 * reorders, fuses or starts from the first term comes out different.
 */
#include "skin.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures;

static void expect_float(const char *what, float got, float want) {
  if (got != want || signbit(got) != signbit(want)) {
    fprintf(stderr, "FAIL %s: got %.9g, want %.9g\n", what, (double)got,
            (double)want);
    failures++;
  }
}

/* Matrix `m` of `matrices` with lane 0 of its four rows set. */
static void set_lane0(float *matrices, unsigned m, float r0, float r1, float r2,
                      float r3) {
  float *matrix = matrices + m * SKIN_MATRIX_FLOATS;
  matrix[0] = r0;
  matrix[4] = r1;
  matrix[8] = r2;
  matrix[12] = r3;
}

static void test_row_order(void) {
  float matrices[2 * SKIN_MATRIX_FLOATS];
  const float one[4] = {1.0f, 1.0f, 1.0f, 1.0f};
  float out[3];
  memset(matrices, 0, sizeof matrices);
  /* row0 + row1 cancel before row2's 1 is added. */
  set_lane0(matrices, 0, 1e8f, -1e8f, 1.0f, 0.0f);
  /* row2's 1 is lost to row0 before row3 cancels it. */
  set_lane0(matrices, 1, 1e8f, 0.0f, 1.0f, -1e8f);
  skin_rigid_vertex(out, one, 0, matrices);
  expect_float("row2 after row0 + row1", out[0], 1.0f);
  skin_rigid_vertex(out, one, 1, matrices);
  expect_float("row3 last", out[0], 0.0f);
}

/* (1 + 2^-12)^2 rounds to 1 + 2^-11; fused into the add, 2^-24 survives. */
static void test_products_round(void) {
  const float a = 1.0f + ldexpf(1.0f, -12);
  float matrices[SKIN_MATRIX_FLOATS] = {0};
  const float position[4] = {a, 1.0f, 0.0f, 1.0f};
  float out[3];
  set_lane0(matrices, 0, a, -(1.0f + ldexpf(1.0f, -11)), 0.0f, 0.0f);
  skin_rigid_vertex(out, position, 0, matrices);
  expect_float("product rounded before the add", out[0], 0.0f);
}

static void test_blend(void) {
  float matrices[3 * SKIN_MATRIX_FLOATS];
  const float one[4] = {1.0f, 1.0f, 1.0f, 1.0f};
  const uint8_t indices[3] = {0, 1, 2};
  const float weights[3] = {1.0f, 1.0f, 1.0f};
  float out[3];
  memset(matrices, 0, sizeof matrices);
  /* Bone order: 1e8 + 1 loses the 1, then -1e8 cancels. */
  set_lane0(matrices, 0, 1e8f, 0.0f, 0.0f, 0.0f);
  set_lane0(matrices, 1, 1.0f, 0.0f, 0.0f, 0.0f);
  set_lane0(matrices, 2, -1e8f, 0.0f, 0.0f, 0.0f);
  skin_blend_vertex(out, one, indices, weights, 3u, matrices);
  expect_float("bones summed in order", out[0], 0.0f);

  /* The weight scales each bone's transform, not the sum. */
  const float halves[2] = {0.5f, 0.25f};
  const uint8_t twice[2] = {1, 1};
  skin_blend_vertex(out, one, twice, halves, 2u, matrices);
  expect_float("weights per bone", out[0], 0.75f);
}

/* The rigid transform keeps -0; the blend's +0 accumulator does not. */
static void test_negative_zero(void) {
  float matrices[SKIN_MATRIX_FLOATS] = {0};
  const float position[4] = {1.0f, 1.0f, 1.0f, 1.0f};
  const uint8_t index = 0;
  const float weight = 1.0f;
  float out[3];
  set_lane0(matrices, 0, -0.0f, -0.0f, -0.0f, -0.0f);
  skin_rigid_vertex(out, position, 0, matrices);
  expect_float("rigid -0", out[0], -0.0f);
  skin_blend_vertex(out, position, &index, &weight, 1u, matrices);
  expect_float("blend from +0", out[0], 0.0f);
}

int main(void) {
  test_row_order();
  test_products_round();
  test_blend();
  test_negative_zero();
  if (failures) {
    fprintf(stderr, "%d failure(s)\n", failures);
    return 1;
  }
  printf("skin: ok\n");
  return 0;
}
