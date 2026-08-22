#include "d3d8_texture_luma.h"
#include "d3d8_types.h"

#include <stdio.h>

int main(void)
{
    static const uint8_t black[4] = { 0, 0, 0, 255 };
    static const uint8_t white[4] = { 255, 255, 255, 255 };
    D3D8TextureLumaStats stats;
    int failures = 0;
    d3d8_texture_luma_note(10, D3DFMT_A8R8G8B8, 1, 1, black, sizeof(black));
    d3d8_texture_luma_note(10, D3DFMT_A8R8G8B8, 1, 1, white, sizeof(white));
    d3d8_texture_luma_note(11, D3DFMT_X8R8G8B8, 1, 1, black, sizeof(black));
    d3d8_texture_luma_note(12, 0, 1, 1, white, sizeof(white));
    d3d8_texture_luma_get_stats(&stats);
    failures += stats.textures != 2;
    failures += stats.unreadable_uploads != 1 || stats.dropped_textures != 0;
    failures += stats.mean_luma < 127.4 || stats.mean_luma > 127.6;
    printf("D3D8 texture luma: %s -- handle replacement, BGRA formats, and "
           "the unreadable denominator retained\n",
           failures ? "FAILED" : "PASSED");
    return failures != 0;
}
