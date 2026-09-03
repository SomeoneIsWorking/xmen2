#include "dialog_selection_scale_policy.h"

#include "retail_ui_design.h"

enum { RETAIL_SCALE_REFERENCE_BITS = 0x3fab020c };

static float retail_scale_reference(void) {
  union {
    uint32_t bits;
    float value;
  } exact = {RETAIL_SCALE_REFERENCE_BITS};
  return exact.value;
}

float x2_dialog_selection_retail_scale(uint32_t output_height) {
  const float per_pixel = 0.0007f;

  return retail_scale_reference() - (float)output_height * per_pixel;
}

float x2_dialog_selection_scale(uint32_t output_height) {
  uint32_t layout_height = output_height;

  if (layout_height > X2_RETAIL_UI_DESIGN_HEIGHT)
    layout_height = X2_RETAIL_UI_DESIGN_HEIGHT;
  return x2_dialog_selection_retail_scale(layout_height);
}
