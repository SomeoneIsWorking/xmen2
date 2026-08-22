/* The upload-staging lifetime contract, exercised on the real SDL GPU path. */
#include "gpu_device.h"
#include "gpu_draw.h"

#include <stdio.h>
#include <string.h>

int gpu_upload_reuse_selftest(void)
{
    unsigned char data[64];
    unsigned long long draw_ns, upload_ns, alloc_ns, submit_ns;
    unsigned long long creates_before, creates_after;
    unsigned long uploads_before, uploads_after, submits;
    GpuBuffer buffer;
    int ok;

    printf("\n=== gpu upload selftest: one resource retains its staging "
           "allocation ===\n");
    if (!gpu_device_create()) {
        printf("gpu upload selftest: FAILED -- no GPU device.\n");
        return 1;
    }
    memset(data, 0x5a, sizeof data);
    buffer = gpu_buffer_create(GPU_BUF_VERTEX, sizeof data);
    if (!buffer) {
        printf("gpu upload selftest: FAILED -- no buffer.\n");
        gpu_device_destroy();
        return 1;
    }
    gpu_draw_perf(&draw_ns, &upload_ns, &alloc_ns, &submit_ns,
                  &creates_before, &uploads_before, &submits);
    ok = gpu_buffer_upload(buffer, 0, data, 16)
         && gpu_buffer_upload(buffer, 16, data + 16, 32);
    gpu_draw_perf(&draw_ns, &upload_ns, &alloc_ns, &submit_ns,
                  &creates_after, &uploads_after, &submits);
    gpu_buffer_destroy(buffer);
    gpu_device_destroy();

    if (!ok || creates_after - creates_before != 1
        || uploads_after - uploads_before != 2) {
        printf("gpu upload selftest: FAILED -- two uploads made %llu "
               "transfer allocation(s) and %lu successful upload(s); "
               "expected one retained allocation and two uploads.\n",
               creates_after - creates_before,
               uploads_after - uploads_before);
        return 1;
    }
    printf("gpu upload selftest: PASSED -- two uploads reused one retained "
           "transfer allocation.\n");
    return 0;
}

/*
 * A dynamic buffer may be drawn, discarded, rewritten and drawn again before
 * Present. The two draws must retain the bytes that were current when each
 * draw was recorded. Submitting the rewrite ahead of the still-open frame
 * command buffer without cycling the destination makes both draws read the
 * second set of bytes instead.
 */
int gpu_upload_order_selftest(void)
{
    struct Vertex { float x, y, z, rhw; unsigned color; };
    static const struct Vertex first[3] = {
        { 2.0f,  2.0f, 0.5f, 1.0f, 0xFFFF0000u },
        { 30.0f, 2.0f, 0.5f, 1.0f, 0xFFFF0000u },
        { 16.0f, 62.0f, 0.5f, 1.0f, 0xFFFF0000u }
    };
    static const struct Vertex second[3] = {
        { 34.0f, 2.0f, 0.5f, 1.0f, 0xFF00FF00u },
        { 62.0f, 2.0f, 0.5f, 1.0f, 0xFF00FF00u },
        { 48.0f, 62.0f, 0.5f, 1.0f, 0xFF00FF00u }
    };
    static unsigned pixels[64 * 64];
    GpuBuffer buffer;
    GpuDraw draw;
    int ok;

    printf("\n=== gpu upload-order selftest: draw, discard, draw retains "
           "both buffer generations ===\n");
    if (!gpu_device_create()) {
        printf("gpu upload-order selftest: FAILED -- no GPU device.\n");
        return 1;
    }
    buffer = gpu_buffer_create(GPU_BUF_VERTEX, sizeof first);
    memset(&draw, 0, sizeof draw);
    draw.vertices = buffer;
    draw.vertex_stride = sizeof first[0];
    draw.prim = GPU_PRIM_TRIANGLELIST;
    draw.prim_count = 1;
    draw.pos_offset = 0;
    draw.pretransformed = 1;
    draw.color_offset = 16;
    draw.uv_offset = -1;
    draw.normal_offset = -1;
    draw.texop = GPU_TEXOP_NONE;
    draw.cull = GPU_CULL_NONE;
    draw.depth_func = GPU_CMP_ALWAYS;

    ok = buffer
      && gpu_buffer_upload(buffer, 0, first, sizeof first)
      && gpu_offscreen_begin(64, 64, 0.0f, 0.0f, 1.0f, 1.0f)
      && gpu_draw(&draw)
      && gpu_buffer_upload(buffer, 0, second, sizeof second)
      && gpu_draw(&draw)
      && gpu_offscreen_read(pixels, sizeof pixels);
    gpu_offscreen_end();
    if (buffer) gpu_buffer_destroy(buffer);
    gpu_device_destroy();

    if (!ok || pixels[32u * 64u + 16u] != 0xFFFF0000u
        || pixels[32u * 64u + 48u] != 0xFF00FF00u) {
        printf("gpu upload-order selftest: FAILED -- left pixel 0x%08x, "
               "right pixel 0x%08x; expected the first red draw and second "
               "green draw to coexist.\n",
               pixels[32u * 64u + 16u], pixels[32u * 64u + 48u]);
        return 1;
    }
    printf("gpu upload-order selftest: PASSED -- the first red draw retained "
           "its buffer generation and the second used the green rewrite.\n");
    return 0;
}
