/*
 * box_cull.c against hand-worked cases. The spill cases are the ones that
 * pin the guest's precision: each builds a sum whose low bits exist only if
 * the named value stayed in an x87 register, so a spill map that rounds one
 * value too many -- or too few -- comes out as 0 where the guest has a power
 * of two, or the reverse.
 */
#include "box_cull.h"

#include "x87.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static int failures;

static void expect_float(const char *what, float got, float want) {
  if (got != want || signbit(got) != signbit(want)) {
    fprintf(stderr, "FAIL %s: got %.9g, want %.9g\n", what, (double)got,
            (double)want);
    failures++;
  }
}

static void expect_verdict(const char *what, BoxCullVerdict got,
                           BoxCullVerdict want) {
  if (got != want) {
    fprintf(stderr, "FAIL %s: got %d, want %d\n", what, (int)got, (int)want);
    failures++;
  }
}

/* max - min per axis; a subtraction whose operands are swapped comes out
   negated. */
static void test_extent(void) {
  const float box[6] = {1.0f, -2.0f, 3.0f, 5.0f, 6.0f, 3.0f};
  float extent[3];
  box_cull_extent(extent, box);
  expect_float("extent x", extent[0], 4.0f);
  expect_float("extent y", extent[1], 8.0f);
  expect_float("extent z", extent[2], 0.0f);
}

static void test_identity_corners(void) {
  const float min[3] = {1.0f, 2.0f, 3.0f};
  const float extent[3] = {4.0f, 8.0f, 16.0f};
  const float matrix[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
  float out[BOX_CULL_CORNER_FLOATS];
  box_cull_corners(out, min, extent, matrix, 0.0f);
  for (unsigned c = 0; c < BOX_CULL_CORNERS; c++) {
    char what[64];
    snprintf(what, sizeof what, "identity corner %u", c);
    expect_float(what, out[c * 4u + 0u], (c & 4u) ? 5.0f : 1.0f);
    expect_float(what, out[c * 4u + 1u], (c & 2u) ? 10.0f : 2.0f);
    expect_float(what, out[c * 4u + 2u], (c & 1u) ? 19.0f : 3.0f);
    expect_float(what, out[c * 4u + 3u], 1.0f);
  }
}

/* x keeps its base in a register, y spills it: corner 4 is base - 1 with
   base = 1 + 2^-30, which survives only unrounded. */
static void test_base_spill(void) {
  const float tiny = ldexpf(1.0f, -30);
  const float min[3] = {tiny, 0.0f, 0.0f};
  const float extent[3] = {-1.0f, 0.0f, 0.0f};
  float matrix[16] = {0};
  matrix[0] = matrix[1] = 1.0f;   /* x and y read min.x and extent.x */
  matrix[12] = matrix[13] = 1.0f; /* and are translated by 1 */
  float out[BOX_CULL_CORNER_FLOATS];
  box_cull_corners(out, min, extent, matrix, 0.0f);
  expect_float("x base stays extended", out[4u * 4u + 0u], tiny);
  expect_float("y base is spilled", out[4u * 4u + 1u], 0.0f);
}

/* y keeps extent.x's term in a register, z spills it: (1 + 2^-23)^2 carries
   a 2^-46 that only the unrounded product has. */
static void test_term_spill(void) {
  const float near_one = 1.0f + ldexpf(1.0f, -23);
  const float min[3] = {0.0f, 0.0f, 0.0f};
  const float extent[3] = {near_one, 0.0f, 0.0f};
  float matrix[16] = {0};
  matrix[1] = matrix[2] = near_one;
  matrix[13] = matrix[14] = -(1.0f + ldexpf(1.0f, -22));
  float out[BOX_CULL_CORNER_FLOATS];
  box_cull_corners(out, min, extent, matrix, 0.0f);
  expect_float("y term0 stays extended", out[4u * 4u + 1u], ldexpf(1.0f, -46));
  expect_float("z term0 is spilled", out[4u * 4u + 2u], 0.0f);
}

static void fill_corners(float corners[BOX_CULL_CORNER_FLOATS], float x,
                         float w) {
  for (unsigned c = 0; c < BOX_CULL_CORNERS; c++) {
    corners[c * 4u + 0u] = x;
    corners[c * 4u + 1u] = 0.0f;
    corners[c * 4u + 2u] = 0.0f;
    corners[c * 4u + 3u] = w;
  }
}

static void test_classify(void) {
  float corners[BOX_CULL_CORNER_FLOATS];
  fill_corners(corners, 0.0f, 1.0f);
  expect_verdict("centred box", box_cull_classify(corners), kBoxCullInside);

  fill_corners(corners, 0.0f, -1.0f);
  expect_verdict("behind the eye", box_cull_classify(corners), kBoxCullOutside);

  /* -0.0 has its sign bit: the guest's first test reads w as an integer. */
  fill_corners(corners, 0.0f, -0.0f);
  expect_verdict("w of -0 counts as behind", box_cull_classify(corners),
                 kBoxCullOutside);

  fill_corners(corners, 2.0f, 1.0f);
  expect_verdict("right of the frustum", box_cull_classify(corners),
                 kBoxCullOutside);

  fill_corners(corners, 0.0f, 1.0f);
  corners[0] = 2.0f;
  expect_verdict("straddling a plane", box_cull_classify(corners),
                 kBoxCullUndecided);

  /* -w - x is +0 exactly on the left plane, and +0 has no sign bit. */
  fill_corners(corners, -1.0f, 1.0f);
  expect_verdict("on the left plane", box_cull_classify(corners),
                 kBoxCullOutside);
}

int main(void) {
#if X86P_EXACT_LONG_DOUBLE && (defined(__x86_64__) || defined(__i386__))
  if (!box_cull_host_exact()) {
    fprintf(stderr, "FAIL: an x87 host whose control word is not the "
                    "process default\n");
    return 1;
  }
  test_extent();
  test_identity_corners();
  test_base_spill();
  test_term_spill();
  test_classify();
#else
  /* No x87 here: the overrides must decline, and that is the whole test. */
  if (box_cull_host_exact()) {
    fprintf(stderr, "FAIL: box_cull claims exactness without an x87 unit\n");
    return 1;
  }
  printf("box_cull: no x87 unit; the overrides run the guest body\n");
#endif
  if (failures) {
    fprintf(stderr, "%d failure(s)\n", failures);
    return 1;
  }
  printf("box_cull: ok\n");
  return 0;
}
