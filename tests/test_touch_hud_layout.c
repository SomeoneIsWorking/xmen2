#include "../src/presentation/touch_hud_layout.h"

#include <math.h>
#include <stdio.h>

static int close_enough(float left, float right)
{
    return fabsf(left - right) < 0.0001f;
}

int main(void)
{
    X2TouchHudPoint retail = {48.0f, 552.0f};
    X2TouchHudPoint root;
    X2TouchHudPoint panel;

    if (!x2_touch_hud_root_to_top_right(retail, 800u, 600u, &root) ||
        !close_enough(root.x, 752.0f) || !close_enough(root.z, 48.0f)) {
        fprintf(stderr, "touch HUD root did not mirror to the top-right\n");
        return 1;
    }
    if (!x2_touch_hud_panel_to_top_left(root, 800u, &panel) ||
        !close_enough(panel.x, retail.x) || !close_enough(panel.z, root.z)) {
        fprintf(stderr, "touch HUD panel did not retain the top-left anchor\n");
        return 1;
    }
    if (x2_touch_hud_root_to_top_right(retail, 0u, 600u, &root) ||
        x2_touch_hud_panel_to_top_left((X2TouchHudPoint){NAN, 1.0f},
                                       800u, &panel)) {
        fprintf(stderr, "touch HUD policy accepted an invalid transform\n");
        return 1;
    }
    puts("touch HUD layout: edge relocation and refusals passed");
    return 0;
}
