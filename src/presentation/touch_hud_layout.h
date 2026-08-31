#ifndef X2_TOUCH_HUD_LAYOUT_H
#define X2_TOUCH_HUD_LAYOUT_H

#include <stdint.h>

typedef struct X2TouchHudPoint {
    float x;
    float z;
} X2TouchHudPoint;

/* The retail HUD is anchored at the bottom-left. Touch mode mirrors that
 * authored anchor across both output axes for the party cross. Per-character
 * panels are then mirrored across X once more, keeping their new top edge. */
int x2_touch_hud_root_to_top_right(X2TouchHudPoint point,
                                   uint32_t width, uint32_t height,
                                   X2TouchHudPoint *result);
int x2_touch_hud_panel_to_top_left(X2TouchHudPoint point,
                                   uint32_t width,
                                   X2TouchHudPoint *result);

#endif /* X2_TOUCH_HUD_LAYOUT_H */
