#pragma once

#include "../config/hud_settings.h"
#include "touch_layout.h"

namespace x2::presentation {

struct HudSpace { /* Retail X/Z plane: Z increases upward. */
  float left, top, width, height;
};

struct HudTransform {
  float scale, x, z;
};

/* Inside a potion ring: where its icon is drawn, and the badge its count is
   drawn in at the ring's lower right. */
X2Rect hud_potion_icon(X2Rect ring);
X2Rect hud_potion_count(X2Rect ring);

int hud_layout_mobile(const X2HudSettings *settings, int touch_enabled);
/* The mobile placement for this viewport. `row_top` is the top edge, in
   output pixels, of the menu-icon row the game's mouse overlay draws at the
   top centre, or a negative value before the game has drawn it: the vitals
   and portraits share that row's top, so the whole top band reads as one
   line. Without it they sit at the safe area's top, inset. */
int hud_layout_build(X2LayoutViewport viewport, const X2HudSettings *settings,
                     float row_top, X2HudPlacement *out);
HudSpace hud_space(float aspect, float scale_x, float scale_z);
/* Fit a retail source rectangle (left, top, width, height) into an output
   rectangle, preserving aspect. The source top is the greatest Z value. */
HudTransform hud_fit(HudSpace space, float output_width, float output_height,
                     HudSpace source, X2Rect target);
void hud_transform_point(HudTransform transform, float xyz[3]);
void hud_transform_matrix(HudTransform transform, float matrix[16]);
X2Rect hud_output_rect(HudSpace space, float output_width, float output_height,
                       float x, float z, float radius);

} // namespace x2::presentation
