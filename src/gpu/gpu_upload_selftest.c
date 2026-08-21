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
