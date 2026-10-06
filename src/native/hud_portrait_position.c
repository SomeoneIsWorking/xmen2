#include "hud_portrait_position.h"

/* 005a16d0..005a1725. Explicit float stores preserve the original x87 spill
 * boundaries; x, y and z deliberately do not use one rearranged formula. */
void x2_hud_portrait_position(const X2HudPortraitAnchor *parent,
                              const X2HudPortraitAnchor *local,
                              float output[3]) {
  const long double scale = local->scale;
  const long double parent_scale = parent->scale;
  const float x = (float)((scale * local->xyz[0]) * parent_scale);
  const float y_local = (float)(scale * local->xyz[1]);
  const float z_local = (float)(scale * local->xyz[2]);
  const float y = (float)((long double)y_local * parent_scale);
  output[0] = (float)((long double)x + parent->xyz[0]);
  output[1] = (float)((long double)y + parent->xyz[1]);
  output[2] = (float)((long double)z_local * parent_scale + parent->xyz[2]);
}
