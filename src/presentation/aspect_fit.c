#include "aspect_fit.h"

int x2_aspect_fit(uint32_t outer_width, uint32_t outer_height,
                  uint32_t inner_width, uint32_t inner_height,
                  X2AspectRect *out)
{
    uint64_t width, height;

    if (!out || !outer_width || !outer_height || !inner_width || !inner_height)
        return 0;

    if ((uint64_t)outer_width * inner_height
        <= (uint64_t)outer_height * inner_width) {
        width = outer_width;
        height = (uint64_t)outer_width * inner_height / inner_width;
    } else {
        height = outer_height;
        width = (uint64_t)outer_height * inner_width / inner_height;
    }
    if (!width || !height || width > outer_width || height > outer_height)
        return 0;

    out->x = (outer_width - (uint32_t)width) / 2u;
    out->y = (outer_height - (uint32_t)height) / 2u;
    out->width = (uint32_t)width;
    out->height = (uint32_t)height;
    return 1;
}
