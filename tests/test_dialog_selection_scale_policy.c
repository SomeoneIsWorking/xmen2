#include "dialog_selection_scale_policy.h"

#include <assert.h>
#include <math.h>

static void expect_near(float actual, float expected) {
  assert(fabsf(actual - expected) < 0.000001f);
}

int main(void) {
  expect_near(x2_dialog_selection_retail_scale(480), 1.0f);
  expect_near(x2_dialog_selection_retail_scale(600), 0.916f);
  expect_near(x2_dialog_selection_retail_scale(720), 0.832f);
  expect_near(x2_dialog_selection_retail_scale(2160), -0.176f);
  expect_near(x2_dialog_selection_scale(480), 1.0f);
  expect_near(x2_dialog_selection_scale(600), 0.916f);
  expect_near(x2_dialog_selection_scale(720), 0.916f);
  expect_near(x2_dialog_selection_scale(2160), 0.916f);
  /* The row's translation moves 7.0 per unit of scale (#185): nothing to
     correct through the reference, and 7 * (0.916 + 0.176) at 2160. */
  expect_near(x2_dialog_selection_offset_correction(600), 0.0f);
  expect_near(x2_dialog_selection_offset_correction(480), 0.0f);
  assert(fabsf(x2_dialog_selection_offset_correction(720) - 0.588f) < 0.0001f);
  assert(fabsf(x2_dialog_selection_offset_correction(2160) - 7.644f) < 0.0001f);
  return 0;
}
