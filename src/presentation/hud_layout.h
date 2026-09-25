#ifndef X2_HUD_LAYOUT_H
#define X2_HUD_LAYOUT_H

#include "../config/hud_settings.h"
#include "touch_layout.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  float left, top, width, height;
} X2HudSpace; /* Retail X/Z plane: Z increases upward. */

typedef struct {
  float scale, x, z;
} X2HudTransform;

/* The retail potion kinds, in the order CHud stacks them. */
enum { X2_HUD_POTION_HEALTH = 0, X2_HUD_POTION_ENERGY = 1, X2_HUD_POTIONS = 2 };

typedef struct {
  X2Rect vitals;
  /* One ring per potion, side by side under the vitals: square, so a touch
     button's circle is inscribed in it. */
  X2Rect potions[X2_HUD_POTIONS];
  X2Rect portraits[4], selector;
} X2HudPlacement;

/* Inside a potion ring: where its icon is drawn, and the badge its count is
   drawn in at the ring's lower right. */
X2Rect x2_hud_potion_icon(X2Rect ring);
X2Rect x2_hud_potion_count(X2Rect ring);

int x2_hud_layout_mobile(const X2HudSettings *settings, int touch_enabled);
int x2_hud_layout_build(X2LayoutViewport viewport,
                        const X2HudSettings *settings, X2HudPlacement *out);
X2HudSpace x2_hud_space(float aspect, float scale_x, float scale_z);
/* Fit a retail source rectangle (left, top, width, height) into an output
   rectangle, preserving aspect. The source top is the greatest Z value. */
X2HudTransform x2_hud_fit(X2HudSpace space, float output_width,
                          float output_height, X2HudSpace source,
                          X2Rect target);
void x2_hud_transform_point(X2HudTransform transform, float xyz[3]);
void x2_hud_transform_matrix(X2HudTransform transform, float matrix[16]);
X2Rect x2_hud_output_rect(X2HudSpace space, float output_width,
                          float output_height, float x, float z, float radius);

#ifdef __cplusplus
}
#endif
#endif
