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
  return 0;
}
