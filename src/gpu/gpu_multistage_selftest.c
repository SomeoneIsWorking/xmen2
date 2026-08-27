/* Pixel proof for stage-0 animation and the observed second texture stage. */
#include "gpu_device.h"
#include "gpu_draw.h"

#include <stdio.h>
#include <string.h>

#define TEST_SIZE 32

int gpu_multistage_selftest(void)
{
#ifndef X2_WITH_SDL
    printf("gpu multistage selftest: SKIPPED -- built without SDL.\n");
    return 77;
#else
    struct Vertex { float x, y, z, rhw; float nx, ny, nz; float u, v; };
    static const struct Vertex quad[6] = {
        {0,0,0.5f,1, 1,0,0, 1,0}, {TEST_SIZE,0,0.5f,1, 1,0,0, 1,0},
        {0,TEST_SIZE,0.5f,1, 1,0,0, 1,0},
        {0,TEST_SIZE,0.5f,1, 1,0,0, 1,0},
        {TEST_SIZE,0,0.5f,1, 1,0,0, 1,0},
        {TEST_SIZE,TEST_SIZE,0.5f,1, 1,0,0, 1,0}
    };
    static const uint32_t base_pixels[2] = {0xffff0000u, 0xff00ff00u};
    static const uint32_t stage1[2] = {0xff000000u, 0xffffffffu};
    static uint32_t pixels[TEST_SIZE * TEST_SIZE];
    static uint32_t no_mip_pixels[TEST_SIZE * TEST_SIZE];
    static uint32_t mip_pixels[TEST_SIZE * TEST_SIZE];
    static const float identity[16] = {
        1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1
    };
    GpuBuffer vertices;
    GpuTexture base, detail, mipped;
    GpuDraw draw;
    uint32_t got;
    int fails = 0;

    puts("\n=== gpu multistage selftest: camera-normal COUNT2 stage 1 ===");
    if (!gpu_device_create()) return 1;
    vertices = gpu_buffer_create(GPU_BUF_VERTEX, sizeof quad);
    base = gpu_texture_create(2, 1, GPU_FMT_BGRA8, 1);
    detail = gpu_texture_create(2, 1, GPU_FMT_BGRA8, 1);
    if (!vertices || !base || !detail
            || !gpu_buffer_upload(vertices, 0, quad, sizeof quad)
            || !gpu_texture_upload(base, 0, base_pixels, sizeof base_pixels)
            || !gpu_texture_upload(detail, 0, stage1, sizeof stage1)) {
        puts("gpu multistage selftest: FAILED -- resources did not upload");
        gpu_device_destroy();
        return 1;
    }

    memset(&draw, 0, sizeof draw);
    draw.vertices = vertices;
    draw.vertex_stride = sizeof quad[0];
    draw.prim = GPU_PRIM_TRIANGLELIST;
    draw.prim_count = 2;
    draw.pos_offset = 0;
    draw.pretransformed = 1;
    draw.color_offset = -1;
    draw.specular_offset = -1;
    draw.uv_offset = 28;
    draw.normal_offset = 16;
    draw.texture = base;
    draw.texop = GPU_TEXOP_SELECT_TEXTURE;
    draw.texture_transform = 2;
    memcpy(draw.texture_matrix, identity, sizeof identity);
    /* u=1 becomes .75. If stage-0's matrix is ignored it wraps to RED. */
    draw.texture_matrix[0] = 0.25f;
    draw.texture_matrix[12] = 0.5f;
    draw.texture_point = 1;
    draw.texture1 = detail;
    draw.texop1 = GPU_TEXOP_MODULATE;
    draw.alpha_op1 = GPU_TEXOP_MODULATE;
    draw.color_arg1_1 = GPU_TA_TEXTURE;
    draw.color_arg2_1 = GPU_TA_CURRENT;
    draw.alpha_arg1_1 = GPU_TA_TEXTURE;
    draw.alpha_arg2_1 = GPU_TA_CURRENT;
    draw.texgen1 = GPU_TEXGEN_CAMERA_NORMAL;
    draw.texture_transform1 = 2;
    memcpy(draw.worldview, identity, sizeof identity);
    memcpy(draw.texture_matrix1, identity, sizeof identity);
    /* +X becomes u=.75. Ignoring stage 1 leaves u=1, which wraps to the
       BLACK control texel; the evidenced path samples WHITE. */
    draw.texture_matrix1[0] = 0.25f;
    draw.texture_matrix1[12] = 0.5f;
    draw.texture_matrix1[13] = 0.5f;
    draw.texture_point1 = 1;
    draw.cull = GPU_CULL_NONE;
    draw.depth_func = GPU_CMP_ALWAYS;

    if (!gpu_offscreen_begin(TEST_SIZE, TEST_SIZE, 0, 0, 1, 1)
            || !gpu_draw(&draw)
            || !gpu_offscreen_read(pixels, sizeof pixels)) {
        puts("gpu multistage selftest: FAILED -- draw/readback did not run");
        gpu_offscreen_end();
        gpu_device_destroy();
        return 1;
    }
    gpu_offscreen_end();
    got = pixels[(TEST_SIZE / 2) * TEST_SIZE + TEST_SIZE / 2];
    if (got != 0xff00ff00u) {
        printf("gpu multistage selftest: FAILED -- centre is 0x%08x, expected "
               "green; red means stage 0's transform was ignored, while "
               "black means stage 1's camera-normal transform was ignored.\n",
               got);
        fails++;
    }

    {
        static const struct Vertex mip_quad[6] = {
            {0,0,0.5f,1, 0,0,1, 0,0},
            {TEST_SIZE,0,0.5f,1, 0,0,1, 64,0},
            {0,TEST_SIZE,0.5f,1, 0,0,1, 0,64},
            {0,TEST_SIZE,0.5f,1, 0,0,1, 0,64},
            {TEST_SIZE,0,0.5f,1, 0,0,1, 64,0},
            {TEST_SIZE,TEST_SIZE,0.5f,1, 0,0,1, 64,64}
        };
        uint32_t levels[16 * 16 + 8 * 8 + 4 * 4 + 2 * 2 + 1];
        size_t offset = 0;
        int level, x;

        for (level = 0; level < 5; level++) {
            int side = 16 >> level;
            uint32_t colour = level == 4 ? 0xff00ff00u : 0xffff0000u;
            for (x = 0; x < side * side; x++) levels[offset + x] = colour;
            offset += (size_t)side * (size_t)side;
        }
        mipped = gpu_texture_create(16, 16, GPU_FMT_BGRA8, 5);
        if (!mipped || !gpu_buffer_upload(vertices, 0, mip_quad,
                                           sizeof mip_quad)) {
            puts("gpu multistage selftest: FAILED -- mip resources did not upload");
            fails++;
        } else {
            offset = 0;
            for (level = 0; level < 5; level++) {
                int side = 16 >> level;
                size_t bytes = (size_t)side * (size_t)side * sizeof(uint32_t);
                if (!gpu_texture_upload(mipped, (uint32_t)level,
                                        levels + offset, (uint32_t)bytes))
                    fails++;
                offset += (size_t)side * (size_t)side;
            }
            memset(&draw, 0, sizeof draw);
            draw.vertices = vertices;
            draw.vertex_stride = sizeof mip_quad[0];
            draw.prim = GPU_PRIM_TRIANGLELIST;
            draw.prim_count = 2;
            draw.pos_offset = 0;
            draw.pretransformed = 1;
            draw.color_offset = -1;
            draw.specular_offset = -1;
            draw.uv_offset = 28;
            draw.normal_offset = 16;
            draw.texture = mipped;
            draw.texop = GPU_TEXOP_SELECT_TEXTURE;
            draw.texture_point = 1;
            draw.texture_min_filter = 1;
            draw.cull = GPU_CULL_NONE;
            draw.depth_func = GPU_CMP_ALWAYS;

            draw.texture_mip = 0;
            if (!gpu_offscreen_begin(TEST_SIZE, TEST_SIZE, 0, 0, 1, 1)
                    || !gpu_draw(&draw)
                    || !gpu_offscreen_read(no_mip_pixels,
                                           sizeof no_mip_pixels)) {
                puts("gpu multistage selftest: FAILED -- no-mip control did not run");
                fails++;
            }
            gpu_offscreen_end();

            draw.texture_mip = 1;
            if (!gpu_offscreen_begin(TEST_SIZE, TEST_SIZE, 0, 0, 1, 1)
                    || !gpu_draw(&draw)
                    || !gpu_offscreen_read(mip_pixels, sizeof mip_pixels)) {
                puts("gpu multistage selftest: FAILED -- mip draw did not run");
                fails++;
            }
            gpu_offscreen_end();

            if (no_mip_pixels[(TEST_SIZE / 2) * TEST_SIZE + TEST_SIZE / 2]
                        != 0xffff0000u
                    || mip_pixels[(TEST_SIZE / 2) * TEST_SIZE + TEST_SIZE / 2]
                        != 0xff00ff00u) {
                printf("gpu multistage selftest: FAILED -- mip control/mipped "
                       "centres are 0x%08x/0x%08x, expected red/green.\n",
                       no_mip_pixels[(TEST_SIZE / 2) * TEST_SIZE + TEST_SIZE / 2],
                       mip_pixels[(TEST_SIZE / 2) * TEST_SIZE + TEST_SIZE / 2]);
                fails++;
            }
        }
    }
    gpu_device_destroy();
    printf("gpu multistage selftest: %s\n", fails ? "FAILED"
           : "PASSED -- both texture stages use transformed coordinates and "
             "minification reaches the resident mip chain");
    return fails;
#endif
}
