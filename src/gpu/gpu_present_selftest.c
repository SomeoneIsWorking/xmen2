/* Pixel-level proof of the logical-backbuffer presentation contract. */
#include "gpu_device.h"
#include "gpu_capture.h"
#include "gpu_capture_internal.h"
#include "gpu_internal.h"
#include "gpu_present.h"

#include <SDL3/SDL.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define OUTPUT_SIZE 12u

static int submit_and_wait(SDL_GPUCommandBuffer *command_buffer)
{
    SDL_GPUFence *fence;

    fence = SDL_SubmitGPUCommandBufferAndAcquireFence(command_buffer);
    if (!fence) {
        fprintf(stderr, "gpu present selftest: submit failed: %s\n",
                SDL_GetError());
        return 0;
    }
    SDL_WaitForGPUFences(g_gpu, true, &fence, 1);
    SDL_ReleaseGPUFence(g_gpu, fence);
    return 1;
}

static SDL_GPUTexture *make_output(void)
{
    SDL_GPUTextureCreateInfo info;

    memset(&info, 0, sizeof info);
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
    info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    info.width = OUTPUT_SIZE;
    info.height = OUTPUT_SIZE;
    info.layer_count_or_depth = 1;
    info.num_levels = 1;
    return SDL_CreateGPUTexture(g_gpu, &info);
}

static int upload_scene(SDL_GPUTexture *scene, uint32_t width, uint32_t height,
                        const uint32_t *pixels)
{
    SDL_GPUTransferBufferCreateInfo info;
    SDL_GPUTransferBuffer *transfer;
    SDL_GPUTextureTransferInfo source;
    SDL_GPUTextureRegion destination;
    SDL_GPUCommandBuffer *command_buffer;
    SDL_GPUCopyPass *copy_pass;
    uint32_t bytes = width * height * sizeof *pixels;
    void *mapped;
    int ok;

    memset(&info, 0, sizeof info);
    info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    info.size = bytes;
    transfer = SDL_CreateGPUTransferBuffer(g_gpu, &info);
    if (!transfer) return 0;
    mapped = SDL_MapGPUTransferBuffer(g_gpu, transfer, false);
    if (!mapped) {
        SDL_ReleaseGPUTransferBuffer(g_gpu, transfer);
        return 0;
    }
    memcpy(mapped, pixels, bytes);
    SDL_UnmapGPUTransferBuffer(g_gpu, transfer);

    command_buffer = SDL_AcquireGPUCommandBuffer(g_gpu);
    if (!command_buffer) {
        SDL_ReleaseGPUTransferBuffer(g_gpu, transfer);
        return 0;
    }
    copy_pass = SDL_BeginGPUCopyPass(command_buffer);
    memset(&source, 0, sizeof source);
    memset(&destination, 0, sizeof destination);
    source.transfer_buffer = transfer;
    destination.texture = scene;
    destination.w = width;
    destination.h = height;
    destination.d = 1;
    SDL_UploadToGPUTexture(copy_pass, &source, &destination, false);
    SDL_EndGPUCopyPass(copy_pass);
    ok = submit_and_wait(command_buffer);
    SDL_ReleaseGPUTransferBuffer(g_gpu, transfer);
    return ok;
}

static int read_output(SDL_GPUTexture *output, uint32_t *pixels)
{
    SDL_GPUTransferBufferCreateInfo info;
    SDL_GPUTransferBuffer *transfer;
    SDL_GPUTextureRegion source;
    SDL_GPUTextureTransferInfo destination;
    SDL_GPUCommandBuffer *command_buffer;
    SDL_GPUCopyPass *copy_pass;
    void *mapped;
    int ok;

    memset(&info, 0, sizeof info);
    info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    info.size = OUTPUT_SIZE * OUTPUT_SIZE * sizeof *pixels;
    transfer = SDL_CreateGPUTransferBuffer(g_gpu, &info);
    if (!transfer) return 0;
    command_buffer = SDL_AcquireGPUCommandBuffer(g_gpu);
    if (!command_buffer) {
        SDL_ReleaseGPUTransferBuffer(g_gpu, transfer);
        return 0;
    }
    copy_pass = SDL_BeginGPUCopyPass(command_buffer);
    memset(&source, 0, sizeof source);
    memset(&destination, 0, sizeof destination);
    source.texture = output;
    source.w = OUTPUT_SIZE;
    source.h = OUTPUT_SIZE;
    source.d = 1;
    destination.transfer_buffer = transfer;
    SDL_DownloadFromGPUTexture(copy_pass, &source, &destination);
    SDL_EndGPUCopyPass(copy_pass);
    ok = submit_and_wait(command_buffer);
    if (ok) {
        mapped = SDL_MapGPUTransferBuffer(g_gpu, transfer, false);
        ok = mapped != NULL;
        if (mapped) {
            memcpy(pixels, mapped, info.size);
            SDL_UnmapGPUTransferBuffer(g_gpu, transfer);
        }
    }
    SDL_ReleaseGPUTransferBuffer(g_gpu, transfer);
    return ok;
}

static int composite_case(uint32_t scene_width, uint32_t scene_height,
                          const uint32_t *scene_pixels, uint32_t *output_pixels,
                          uint32_t *capture_pixels)
{
    SDL_GPUTexture *scene;
    SDL_GPUTexture *output;
    SDL_GPUTexture *target;
    SDL_GPUCommandBuffer *command_buffer;
    const unsigned char *captured;
    uint32_t captured_width, captured_height;
    char why[192];
    int ok = 0;

    if (!gpu_present_set_scene_size(scene_width, scene_height)) return 0;
    scene = gpu_present_scene(g_gpu, NULL, NULL);
    output = make_output();
    if (!scene || !output
        || !upload_scene(scene, scene_width, scene_height, scene_pixels))
        goto done;
    if (!gpu_capture_request(why, sizeof why)) goto done;
    target = gpu_capture_frame_target(g_gpu, output,
                                      OUTPUT_SIZE, OUTPUT_SIZE);
    if (target == output) goto done;
    command_buffer = SDL_AcquireGPUCommandBuffer(g_gpu);
    if (!command_buffer) goto done;
    if (!gpu_present_composite(command_buffer, target,
                               OUTPUT_SIZE, OUTPUT_SIZE)) {
        SDL_CancelGPUCommandBuffer(command_buffer);
        goto done;
    }
    if (!gpu_capture_frame_record(g_gpu, command_buffer, target, output,
                                  OUTPUT_SIZE, OUTPUT_SIZE)) {
        SDL_CancelGPUCommandBuffer(command_buffer);
        goto done;
    }
    if (!submit_and_wait(command_buffer)) {
        gpu_capture_frame_complete(g_gpu, 0);
        goto done;
    }
    gpu_capture_frame_complete(g_gpu, 1);
    if (gpu_capture_result(&captured, &captured_width, &captured_height,
                           why, sizeof why) != 1
        || captured_width != OUTPUT_SIZE || captured_height != OUTPUT_SIZE
        || !read_output(output, output_pixels))
        goto done;
    memcpy(capture_pixels, captured,
           OUTPUT_SIZE * OUTPUT_SIZE * sizeof *capture_pixels);
    ok = 1;

done:
    gpu_capture_discard();
    if (output) SDL_ReleaseGPUTexture(g_gpu, output);
    return ok;
}

static uint32_t pixel(const uint32_t *pixels, uint32_t x, uint32_t y)
{
    return pixels[y * OUTPUT_SIZE + x];
}

int gpu_present_selftest(void)
{
    static const uint32_t wide[8] = {
        0xffff0000u, 0xffff0000u, 0xff00ff00u, 0xff00ff00u,
        0xff0000ffu, 0xff0000ffu, 0xffffffffu, 0xffffffffu,
    };
    static const uint32_t tall[8] = {
        0xffff0000u, 0xff00ff00u,
        0xffff0000u, 0xff00ff00u,
        0xff0000ffu, 0xffffffffu,
        0xff0000ffu, 0xffffffffu,
    };
    uint32_t output[OUTPUT_SIZE * OUTPUT_SIZE];
    uint32_t capture[OUTPUT_SIZE * OUTPUT_SIZE];
    const unsigned char *unused;
    uint32_t unused_width, unused_height;
    char why[192];
    int wide_ok, tall_ok, bound_ok;

    printf("\n=== gpu present selftest: logical scene aspect-fit pixels ===\n");
    if (!gpu_device_create()) {
        printf("gpu present selftest: FAILED -- no GPU device.\n");
        return 1;
    }
    wide_ok = composite_case(4, 2, wide, output, capture)
        && !memcmp(output, capture, sizeof output)
        && pixel(output, 6, 1) == 0xff000000u
        && pixel(output, 6, 10) == 0xff000000u
        && pixel(output, 1, 4) == 0xffff0000u
        && pixel(output, 10, 4) == 0xff00ff00u
        && pixel(output, 1, 7) == 0xff0000ffu
        && pixel(output, 10, 7) == 0xffffffffu;
    tall_ok = composite_case(2, 4, tall, output, capture)
        && !memcmp(output, capture, sizeof output)
        && pixel(output, 1, 6) == 0xff000000u
        && pixel(output, 10, 6) == 0xff000000u
        && pixel(output, 4, 1) == 0xffff0000u
        && pixel(output, 7, 1) == 0xff00ff00u
        && pixel(output, 4, 10) == 0xff0000ffu
        && pixel(output, 7, 10) == 0xffffffffu;
    bound_ok = gpu_capture_request(why, sizeof why)
        && gpu_capture_frame_target(g_gpu, NULL, 8192u, 8192u) == NULL
        && gpu_capture_result(&unused, &unused_width, &unused_height,
                              why, sizeof why) < 0
        && strstr(why, "bound") != NULL;
    gpu_capture_discard();
    if (!wide_ok || !tall_ok || !bound_ok) {
        printf("gpu present selftest: FAILED -- wide=%d tall=%d bound=%d; "
               "expected identical retained/output pixels, black bars, four "
               "corner colours and an explicit oversized refusal.\n",
               wide_ok, tall_ok, bound_ok);
    } else {
        printf("gpu present selftest: PASSED -- wide and tall scenes used "
               "the production compositor and retained capture, with black "
               "bars, preserved corner colours and a bounded allocation.\n");
    }
    gpu_device_destroy();
    return wide_ok && tall_ok && bound_ok ? 0 : 1;
}
