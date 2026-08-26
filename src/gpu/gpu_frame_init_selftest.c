/* Prove the initial colour load policy with pixels, not a return value. */
#include "gpu_device.h"
#include "gpu_draw.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TEST_W 64
#define TEST_H 64

int gpu_frame_init_selftest(void)
{
#ifndef X2_WITH_SDL
    printf("gpu frame-init selftest: SKIPPED -- built without SDL. This is "
           "not a pass.\n");
    return 77;
#else
    struct { float x, y, z, rhw; uint32_t color; } tri[3] = {
        { 24.0f, 16.0f, 0.5f, 1.0f, 0xFFFF0000u },
        { 48.0f, 48.0f, 0.5f, 1.0f, 0xFFFF0000u },
        { 16.0f, 48.0f, 0.5f, 1.0f, 0xFFFF0000u }
    };
    static uint32_t img[TEST_W * TEST_H];
    GpuBuffer vb;
    GpuDraw d;
    uint32_t centre, edge_a, edge_b;
    int fails = 0;

    printf("\n=== gpu frame-init selftest: untouched pixels start black ===\n");
    if (!gpu_device_create()) {
        printf("gpu frame-init selftest: FAILED -- no GPU device.\n");
        return 1;
    }
    vb = gpu_buffer_create(GPU_BUF_VERTEX, sizeof tri);
    if (!vb || !gpu_buffer_upload(vb, 0, tri, sizeof tri)) {
        printf("gpu frame-init selftest: FAILED -- no vertex buffer.\n");
        gpu_device_destroy();
        return 1;
    }

    /* Poison every pixel blue, then begin a new logical frame without the
       game issuing Clear. This recreates a loading/UI frame that touches only
       part of the target; preserving or discarding the old attachment is not
       allowed to leak blue or tile memory into its untouched edges. */
    if (!gpu_offscreen_begin(TEST_W, TEST_H, 0.0f, 0.0f, 1.0f, 1.0f)
        || !gpu_offscreen_next_no_clear()) {
        printf("gpu frame-init selftest: FAILED -- could not start the two "
               "off-screen frames.\n");
        gpu_offscreen_end();
        gpu_device_destroy();
        return 1;
    }

    memset(&d, 0, sizeof d);
    d.vertices = vb;
    d.vertex_stride = sizeof tri[0];
    d.prim = GPU_PRIM_TRIANGLELIST;
    d.prim_count = 1;
    d.pos_offset = 0;
    d.pretransformed = 1;
    d.color_offset = 16;
    d.uv_offset = -1;
    d.texop = GPU_TEXOP_NONE;
    d.cull = GPU_CULL_NONE;
    d.depth_func = GPU_CMP_ALWAYS;
    if (!gpu_draw(&d) || !gpu_offscreen_read(img, sizeof img)) {
        printf("gpu frame-init selftest: FAILED -- partial draw/readback did "
               "not complete.\n");
        gpu_offscreen_end();
        gpu_device_destroy();
        return 1;
    }
    gpu_offscreen_end();

    centre = img[32 * TEST_W + 32];
    edge_a = img[1 * TEST_W + 1];
    edge_b = img[62 * TEST_W + 62];
    if (centre != 0xFFFF0000u) {
        printf("gpu frame-init selftest: FAILED -- partial draw is 0x%08x, "
               "expected red.\n", centre);
        fails++;
    }
    if (edge_a != 0xFF000000u || edge_b != 0xFF000000u) {
        printf("gpu frame-init selftest: FAILED -- untouched edges are "
               "0x%08x and 0x%08x, expected opaque black.\n",
               edge_a, edge_b);
        fails++;
    }

    gpu_device_destroy();
    printf("gpu frame-init selftest: %s\n", fails ? "FAILED"
           : "PASSED -- a sparse frame draws its content and initializes "
             "both untouched edges to black");
    return fails ? 1 : 0;
#endif
}
