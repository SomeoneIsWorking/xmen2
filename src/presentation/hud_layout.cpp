#include "hud_layout.h"

#include <math.h>

namespace x2::presentation {

int hud_layout_mobile(const X2HudSettings *settings, int touch_enabled) {
  return settings->layout == X2_HUD_LAYOUT_MOBILE ||
         (settings->layout == X2_HUD_LAYOUT_AUTO && touch_enabled);
}

int hud_layout_build(LayoutViewport v, const X2HudSettings *s, float row_top,
                     HudPlacement *out) {
  Rect validated[kSlotCount];
  if (!out || !x2_hud_settings_valid(s) || !layout_build(v, NULL, validated))
    return 0;
  float width = v.width - v.safe_left - v.safe_right;
  float height = v.height - v.safe_top - v.safe_bottom;
  float short_edge = fminf(width, height);
  float inset = short_edge * (float)s->safe_inset_percent / 100.0f;
  float left = v.safe_left + inset;
  float top =
      isfinite(row_top) && row_top >= 0.0f ? row_top : v.safe_top + inset;
  float right = v.width - v.safe_right - inset;
  float available = (right - left) * 0.4f;
  float bar_width = fminf(
      available, short_edge * 0.42f * (float)s->vitals_scale_percent / 100.0f);
  float bar_height = bar_width * 24.0f / 102.0f;
  float gap = short_edge * 0.02f;
  float potion_size =
      fminf((available - gap) / 2.0f,
            short_edge * 0.15f * (float)s->potions_scale_percent / 100.0f);
  float face =
      fminf(available / 4.0f,
            short_edge * 0.16f * (float)s->portraits_scale_percent / 100.0f);
  HudPlacement layout = {0};
  layout.vitals = (Rect){left, top, left + bar_width, top + bar_height};
  float potion_top = layout.vitals.bottom + gap;
  for (unsigned i = 0; i < kHudPotions; ++i) {
    float x = left + (float)i * (potion_size + gap);
    layout.potions[i] =
        (Rect){x, potion_top, x + potion_size, potion_top + potion_size};
  }
  for (unsigned i = 0; i < 4; ++i) {
    float x = right - face * (float)(4 - i);
    layout.portraits[i] = (Rect){x, top, x + face, top + face};
  }
  /* The console D-pad selector cross is moved offscreen in mobile layout:
     hero selection routes directly through portrait tapping, and its
     directional beams point away from the horizontal portrait row. */
  layout.selector = (Rect){-1000.0f, -1000.0f, -1000.0f, -1000.0f};
  *out = layout;
  return 1;
}

Rect hud_potion_icon(Rect ring) {
  float inset = (ring.right - ring.left) * 0.16f;
  return (Rect){ring.left + inset, ring.top + inset, ring.right - inset,
                ring.bottom - inset};
}

Rect hud_potion_count(Rect ring) {
  float size = (ring.right - ring.left) * 0.36f;
  return (Rect){ring.right - size, ring.bottom - size, ring.right, ring.bottom};
}

HudSpace hud_space(float aspect, float scale_x, float scale_z) {
  float width = scale_x * aspect * 384.0f;
  float height = scale_z * 384.0f;
  return (HudSpace){256.0f - width * 0.5f, 192.0f + height * 0.5f, width,
                    height};
}

HudTransform hud_fit(HudSpace space, float width, float height, HudSpace source,
                     Rect target) {
  float target_width = (target.right - target.left) * space.width / width;
  float target_height = (target.bottom - target.top) * space.height / height;
  float scale =
      fminf(target_width / source.width, target_height / source.height);
  float left = space.left + target.left * space.width / width;
  float top = space.top - target.top * space.height / height;
  return (HudTransform){scale, left - source.left * scale,
                        top - source.top * scale};
}

void hud_transform_point(HudTransform t, float xyz[3]) {
  xyz[0] = xyz[0] * t.scale + t.x;
  xyz[2] = xyz[2] * t.scale + t.z;
}

void hud_transform_matrix(HudTransform t, float matrix[16]) {
  /* Alchemy row vectors. Apply the screen-plane affine AFTER the model pose;
     preserve Y (depth), its basis and all homogeneous components. */
  for (unsigned row = 0; row < 4; ++row) {
    matrix[row * 4] = matrix[row * 4] * t.scale + matrix[row * 4 + 3] * t.x;
    matrix[row * 4 + 2] =
        matrix[row * 4 + 2] * t.scale + matrix[row * 4 + 3] * t.z;
  }
}

Rect hud_output_rect(HudSpace s, float width, float height, float x, float z,
                     float radius) {
  return (Rect){(x - radius - s.left) * width / s.width,
                (s.top - z - radius) * height / s.height,
                (x + radius - s.left) * width / s.width,
                (s.top - z + radius) * height / s.height};
}

} // namespace x2::presentation
