#include "hud_portrait_position.h"

#include <assert.h>
#include <stdio.h>

static void expect(const x2::native::HudPortraitAnchor *parent,
                   const x2::native::HudPortraitAnchor *local, float x,
                   float depth, float z) {
  float output[3];
  x2::native::hud_portrait_position(parent, local, output);
  assert(output[0] == x);
  assert(output[1] == depth);
  assert(output[2] == z);
}

int main(void) {
  const x2::native::HudPortraitAnchor parent = {{10.0f, 200.0f, -20.0f}, 2.0f};
  const x2::native::HudPortraitAnchor local = {{4.0f, -6.0f, 8.0f}, 0.5f};
  expect(&parent, &local, 14.0f, 194.0f, -12.0f);

  const x2::native::HudPortraitAnchor zero_scale = {{100.0f, 200.0f, 300.0f},
                                                    0.0f};
  expect(&zero_scale, &local, 100.0f, 200.0f, 300.0f);

  const x2::native::HudPortraitAnchor reversed = {{1.0f, 2.0f, 3.0f}, -4.0f};
  expect(&reversed, &local, -7.0f, 14.0f, -13.0f);

  const x2::native::HudPortraitAnchor offset = {{-4.0f, 8.0f, -16.0f}, 0.25f};
  const x2::native::HudPortraitAnchor identity = {{0.0f, 0.0f, 0.0f}, 1.0f};
  expect(&offset, &identity, -4.0f, 8.0f, -16.0f);
  puts("HUD portrait position: 4 transforms / 12 coordinates passed");
  return 0;
}
