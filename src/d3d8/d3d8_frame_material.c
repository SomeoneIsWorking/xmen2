#include "d3d8_drawcall.h"

#include <string.h>

void d3d8_frame_material_rgb(const GpuDraw *draw, uint32_t source,
                             const uint8_t *vertex, const float material[4],
                             double out[3])
{
    float color[4];
    uint32_t packed;
    int offset = source == 1u ? draw->color_offset : draw->specular_offset;
    int c;

    if (!draw->color_vertex || source == 0u || offset < 0) {
        for (c = 0; c < 3; c++) out[c] = material[c];
        return;
    }
    memcpy(&packed, vertex + offset, sizeof packed);
    color[0] = (float)((packed >> 16) & 0xffu) / 255.0f;
    color[1] = (float)((packed >> 8) & 0xffu) / 255.0f;
    color[2] = (float)(packed & 0xffu) / 255.0f;
    for (c = 0; c < 3; c++) out[c] = color[c];
}
