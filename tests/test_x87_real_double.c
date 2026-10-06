/*
 * The x87-order overrides where x87_real is a double: the browser, ARM64
 * Linux and Android, and Apple Silicon (x87_exact.h).
 *
 * The shipping box_cull.c, ig_matrix.c and ig_matrix_invert.c are built twice
 * into this test: as usual, where long double is the x87 format and the
 * answers are the guest's bits, and with a 64-bit long double and every
 * public name prefixed `double_`, which is the double configuration. The
 * double answers are held to the exact ones:
 *
 *   - a value whose extended sum needs more than 53 bits comes out
 *     differently, so the second build really is the double one;
 *   - matrix products and box corners are within one float ulp;
 *   - inverses are within X2_INVERT_ULPS float ulps, and singularity is
 *     decided the same way;
 *   - a box's cull verdict from double corners is the exact verdict.
 */
#include "box_cull.h"
#include "ig_matrix.h"
#include "ig_matrix_invert.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* The double build's names (CMakeLists.txt, x2_x87_double_math). */
void double_ig_matrix44_multiply(float out[16], const float a[16],
                                 const float b[16]);
IgInvertVerdict double_ig_matrix44_invert(float out[16], const float m[16],
                                          uint16_t *compare_codes);
void double_box_cull_corners(float out[BOX_CULL_CORNER_FLOATS],
                             const float min[3], const float extent[3],
                             const float matrix[16], float zero);

/* An inverse's elements are each a dozen roundings from their inputs. */
#define X2_INVERT_ULPS 4u
#define CASES 50000u

static int failures;

static void check(int ok, const char *what) {
  if (!ok) {
    printf("  FAIL  %s\n", what);
    failures++;
  } else {
    printf("  pass  %s\n", what);
  }
}

static uint32_t g_rng = 0x2545F491u;

static uint32_t rng(void) {
  g_rng ^= g_rng << 13;
  g_rng ^= g_rng >> 17;
  g_rng ^= g_rng << 5;
  return g_rng;
}

static float uniform(float lo, float hi) {
  return lo + (hi - lo) * (float)(rng() >> 8) * (1.0f / 16777216.0f);
}

/* Distance in float ulps, over floats ordered as integers. */
static uint32_t ulps(float a, float b) {
  int32_t ia;
  int32_t ib;
  memcpy(&ia, &a, sizeof ia);
  memcpy(&ib, &b, sizeof ib);
  ia = ia < 0 ? (int32_t)(0x80000000u - (uint32_t)ia) : ia;
  ib = ib < 0 ? (int32_t)(0x80000000u - (uint32_t)ib) : ib;
  return ia > ib ? (uint32_t)(ia - ib) : (uint32_t)(ib - ia);
}

static void random_matrix(float m[16]) {
  for (unsigned i = 0; i < 16u; i++) {
    m[i] = uniform(-4.0f, 4.0f);
  }
}

/* 1 + 2^-58 - 1 is 2^-58 in 64 bits and 0 in 53: x's base keeps its sum in
   a register. */
static void test_configurations_differ(void) {
  const float min[3] = {1.0f, 0.0f, ldexpf(1.0f, -29)};
  const float extent[3] = {0.0f, 0.0f, 0.0f};
  float matrix[16] = {0};
  matrix[0] = 1.0f;
  matrix[8] = ldexpf(1.0f, -29);
  matrix[12] = -1.0f;
  float exact[BOX_CULL_CORNER_FLOATS];
  float approximate[BOX_CULL_CORNER_FLOATS];
  box_cull_corners(exact, min, extent, matrix, 0.0f);
  double_box_cull_corners(approximate, min, extent, matrix, 0.0f);
  check(exact[0] == ldexpf(1.0f, -58) && approximate[0] == 0.0f,
        "a sum needing 58 bits is exact in one build and not the other");
}

static void test_multiply(void) {
  uint32_t worst = 0;
  unsigned differ = 0;
  for (unsigned n = 0; n < CASES; n++) {
    float a[16];
    float b[16];
    float exact[16];
    float approximate[16];
    random_matrix(a);
    random_matrix(b);
    ig_matrix44_multiply(exact, a, b);
    double_ig_matrix44_multiply(approximate, a, b);
    for (unsigned i = 0; i < 16u; i++) {
      const uint32_t d = ulps(exact[i], approximate[i]);
      differ += d != 0u;
      worst = d > worst ? d : worst;
    }
  }
  printf("  matrix product: %u of %u elements differ, worst %u ulp(s)\n",
         differ, CASES * 16u, worst);
  check(worst <= 1u, "a matrix product is within one ulp of exact");
}

static void test_invert(void) {
  uint32_t worst = 0;
  unsigned differ = 0;
  unsigned verdicts = 0;
  unsigned inverted = 0;
  for (unsigned n = 0; n < CASES; n++) {
    float m[16];
    float exact[16] = {0};
    float approximate[16] = {0};
    uint16_t exact_codes = 0;
    uint16_t approximate_codes = 0;
    random_matrix(m);
    const IgInvertVerdict e = ig_matrix44_invert(exact, m, &exact_codes);
    const IgInvertVerdict a =
        double_ig_matrix44_invert(approximate, m, &approximate_codes);
    verdicts += e != a || exact_codes != approximate_codes;
    if (e != kIgInvertInverted || a != kIgInvertInverted) {
      continue;
    }
    inverted++;
    for (unsigned i = 0; i < 16u; i++) {
      const uint32_t d = ulps(exact[i], approximate[i]);
      differ += d != 0u;
      worst = d > worst ? d : worst;
    }
  }
  float singular[16] = {1, 2, 3, 4, 2, 4, 6, 8, 0, 1, 0, 1, 1, 0, 1, 0};
  float out[16];
  uint16_t codes;
  printf("  inverse: %u inverted, %u elements differ, worst %u ulp(s); %u "
         "verdict(s) differ\n",
         inverted, differ, worst, verdicts);
  check(inverted > CASES / 2u && worst <= X2_INVERT_ULPS && !verdicts &&
            double_ig_matrix44_invert(out, singular, &codes) ==
                kIgInvertSingular,
        "an inverse is within a few ulps of exact, singular or not alike");
}

static void test_box_cull(void) {
  uint32_t worst = 0;
  unsigned differ = 0;
  unsigned verdicts = 0;
  unsigned crossing = 0;
  unsigned outside = 0;
  for (unsigned n = 0; n < CASES; n++) {
    float m[16];
    random_matrix(m);
    m[15] = uniform(2.0f, 6.0f);
    const float min[3] = {uniform(-3.0f, 3.0f), uniform(-3.0f, 3.0f),
                          uniform(-3.0f, 3.0f)};
    const float extent[3] = {uniform(0.0f, 2.0f), uniform(0.0f, 2.0f),
                             uniform(0.0f, 2.0f)};
    float exact[BOX_CULL_CORNER_FLOATS];
    float approximate[BOX_CULL_CORNER_FLOATS];
    box_cull_corners(exact, min, extent, m, 0.0f);
    double_box_cull_corners(approximate, min, extent, m, 0.0f);
    for (unsigned i = 0; i < BOX_CULL_CORNER_FLOATS; i++) {
      const uint32_t d = ulps(exact[i], approximate[i]);
      differ += d != 0u;
      worst = d > worst ? d : worst;
    }
    const BoxCullVerdict verdict = box_cull_classify(exact);
    crossing += verdict == kBoxCullUndecided;
    outside += verdict == kBoxCullOutside;
    verdicts += box_cull_classify(approximate) != verdict;
  }
  printf("  box corners: %u of %u differ, worst %u ulp(s); verdicts %u "
         "crossing, %u outside, %u differ\n",
         differ, CASES * BOX_CULL_CORNER_FLOATS, worst, crossing, outside,
         verdicts);
  check(worst <= 1u, "box corners are within one ulp of exact");
  check(crossing > 1000u && outside > 1000u && !verdicts,
        "every cull verdict from double corners is the exact one");
}

int main(void) {
  test_configurations_differ();
  test_multiply();
  test_invert();
  test_box_cull();
  if (failures) {
    printf("%d failure(s)\n", failures);
    return 1;
  }
  printf("x87_real double: all cases passed\n");
  return 0;
}
