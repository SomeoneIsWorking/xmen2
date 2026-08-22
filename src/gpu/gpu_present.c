/* Logical D3D backbuffer -> physical SDL swapchain presentation. */
#include "gpu_present.h"

#include "aspect_fit.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

static SDL_GPUTexture *g_scene;
static uint32_t g_scene_width, g_scene_height;
static uint32_t g_texture_width, g_texture_height;

int gpu_present_set_scene_size(uint32_t width, uint32_t height)
{
    if (!width || !height) {
        fprintf(stderr, "gpu present: refusing a zero-sized logical D3D "
                        "backbuffer (%ux%u).\n", width, height);
        return 0;
    }
    g_scene_width = width;
    g_scene_height = height;
    return 1;
}

int gpu_present_is_configured(void)
{
    return g_scene_width != 0 && g_scene_height != 0;
}

SDL_GPUTexture *gpu_present_scene(SDL_GPUDevice *device,
                                  uint32_t *width, uint32_t *height)
{
    SDL_GPUTextureCreateInfo info;

    if (!device || !gpu_present_is_configured()) return NULL;
    if (g_scene && (g_texture_width != g_scene_width
                    || g_texture_height != g_scene_height)) {
        SDL_ReleaseGPUTexture(device, g_scene);
        g_scene = NULL;
        g_texture_width = g_texture_height = 0;
    }
    if (!g_scene) {
        memset(&info, 0, sizeof info);
        info.type = SDL_GPU_TEXTURETYPE_2D;
        info.format = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
        info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET
                     | SDL_GPU_TEXTUREUSAGE_SAMPLER;
        info.width = g_scene_width;
        info.height = g_scene_height;
        info.layer_count_or_depth = 1;
        info.num_levels = 1;
        g_scene = SDL_CreateGPUTexture(device, &info);
        if (!g_scene) {
            fprintf(stderr, "gpu present: could not create the logical %ux%u "
                            "D3D backbuffer: %s. The game frame will not be "
                            "drawn directly into the wrong-sized window.\n",
                    g_scene_width, g_scene_height, SDL_GetError());
            return NULL;
        }
        g_texture_width = g_scene_width;
        g_texture_height = g_scene_height;
        printf("gpu present: logical D3D backbuffer is %ux%u; the window is "
               "a separate aspect-fitted output.\n",
               g_scene_width, g_scene_height);
    }
    if (width) *width = g_scene_width;
    if (height) *height = g_scene_height;
    return g_scene;
}

int gpu_present_composite(SDL_GPUCommandBuffer *command_buffer,
                          SDL_GPUTexture *output,
                          uint32_t output_width, uint32_t output_height)
{
    SDL_GPUBlitInfo info;
    X2AspectRect destination;

    if (!command_buffer || !output || !g_scene
        || !x2_aspect_fit(output_width, output_height,
                          g_scene_width, g_scene_height, &destination)) {
        fprintf(stderr, "gpu present: refusing to composite logical %ux%u "
                        "into output %ux%u; a texture or valid size is "
                        "missing.\n",
                g_scene_width, g_scene_height,
                output_width, output_height);
        return 0;
    }

    memset(&info, 0, sizeof info);
    info.source.texture = g_scene;
    info.source.w = g_scene_width;
    info.source.h = g_scene_height;
    info.destination.texture = output;
    info.destination.x = destination.x;
    info.destination.y = destination.y;
    info.destination.w = destination.width;
    info.destination.h = destination.height;
    info.load_op = SDL_GPU_LOADOP_CLEAR;
    info.clear_color.a = 1.0f;
    info.filter = SDL_GPU_FILTER_LINEAR;
    SDL_BlitGPUTexture(command_buffer, &info);
    return 1;
}

void gpu_present_shutdown(SDL_GPUDevice *device)
{
    if (device && g_scene) SDL_ReleaseGPUTexture(device, g_scene);
    g_scene = NULL;
    g_scene_width = 0;
    g_scene_height = 0;
    g_texture_width = 0;
    g_texture_height = 0;
}
