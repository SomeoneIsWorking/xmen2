#include "touch_hud_layout.h"

#include <math.h>

static int valid_point(X2TouchHudPoint point)
{
    return isfinite(point.x) && isfinite(point.z);
}

int x2_touch_hud_root_to_top_right(X2TouchHudPoint point,
                                   uint32_t width, uint32_t height,
                                   X2TouchHudPoint *result)
{
    if (!result || !width || !height || !valid_point(point)) return 0;
    result->x = (float)width - point.x;
    result->z = (float)height - point.z;
    return valid_point(*result);
}

int x2_touch_hud_panel_to_top_left(X2TouchHudPoint point,
                                   uint32_t width,
                                   X2TouchHudPoint *result)
{
    if (!result || !width || !valid_point(point)) return 0;
    result->x = (float)width - point.x;
    result->z = point.z;
    return valid_point(*result);
}
