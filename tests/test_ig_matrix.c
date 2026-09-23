/*
 * ig_matrix.c against hand-worked cases. With b's column 0 all ones, element
 * r of column 0 is the sum of row r of `a` in the guest's order for that row.
 * Each row's four products are set so that only the guest's order gives the
 * expected sum (see test_row_orders). A second case keeps 2^-40 only if the
 * partial sums stay in extended precision.
 */
#include "ig_matrix.h"
#include "x87_exact.h"

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

/* The a-index of each row's products, in the order the guest adds them. */
static const unsigned kRowOrder[4][4] = {
    {3, 2, 0, 1}, {7, 5, 6, 4}, {10, 8, 9, 11}, {15, 13, 14, 12}};

/* Each row's four products take `sequence` in the guest's order. */
static void check_row_orders(const char *name, const float sequence[4],
                             float want) {
  float a[16];
  float b[16];
  float out[16];
  memset(a, 0, sizeof a);
  memset(b, 0, sizeof b);
  b[0] = b[4] = b[8] = b[12] = 1.0f;
  for (unsigned row = 0; row < 4u; row++) {
    for (unsigned k = 0; k < 4u; k++) {
      a[kRowOrder[row][k]] = sequence[k];
    }
  }
  ig_matrix44_multiply(out, a, b);
  for (unsigned row = 0; row < 4u; row++) {
    char what[64];
    snprintf(what, sizeof what, "%s, row %u", name, row);
    expect_float(what, out[row * 4u], want);
  }
}

/* 2^70, -2^70, 1, 0 sums to 1 only if the pair cancels before the 1 is
   added. 2^70, 1, -2^70, 1 sums to 1 only in exactly that order: the last
   two swapped give 0, and the first 1 moved last gives 2. */
static void test_row_orders(void) {
  const float big = ldexpf(1.0f, 70);
  const float cancel_first[4] = {big, -big, 1.0f, 0.0f};
  const float absorb_then_cancel[4] = {big, 1.0f, -big, 1.0f};
  check_row_orders("cancel first", cancel_first, 1.0f);
  check_row_orders("absorb then cancel", absorb_then_cancel, 1.0f);
}

static void test_extended_partials(void) {
  float a[16];
  float b[16];
  float out[16];
  memset(a, 0, sizeof a);
  memset(b, 0, sizeof b);
  b[0] = b[4] = b[8] = b[12] = 1.0f;
  a[3] = 1.0f;
  a[2] = ldexpf(1.0f, -40);
  a[0] = -1.0f;
  ig_matrix44_multiply(out, a, b);
  expect_float("partial sums stay extended", out[0], ldexpf(1.0f, -40));
}

int main(void) {
#if X86P_EXACT_LONG_DOUBLE && (defined(__x86_64__) || defined(__i386__))
  if (!x87_exact_host()) {
    fprintf(stderr, "FAIL: an x87 host whose control word is not the "
                    "process default\n");
    return 1;
  }
  test_row_orders();
  test_extended_partials();
#else
  /* No x87 here: the override must decline, and that is the whole test. */
  if (x87_exact_host()) {
    fprintf(stderr, "FAIL: x87_exact_host claims exactness without an x87 "
                    "unit\n");
    return 1;
  }
  printf("ig_matrix: no x87 unit; the override runs the guest body\n");
#endif
  if (failures) {
    fprintf(stderr, "%d failure(s)\n", failures);
    return 1;
  }
  printf("ig_matrix: ok\n");
  return 0;
}
