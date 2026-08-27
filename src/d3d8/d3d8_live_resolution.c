#include "d3d8_live_resolution.h"

#include "gpu_device.h"

#include <stdio.h>

#define MAX_RESOLUTION_DIMENSION 16384u

typedef struct {
    D3DPRESENT_PARAMETERS *parameters;
    D3D8Surface *backbuffer;
    D3D8Surface *depth;
    D3D8State *state;
} ActivePresentation;

static ActivePresentation g_active;

static int fail(char *why, int whyn, const char *message)
{
    if (why && whyn > 0) snprintf(why, (size_t)whyn, "%s", message);
    return 0;
}

static void resize_surface(D3D8Surface *surface,
                           uint32_t width, uint32_t height)
{
    if (!surface) return;
    surface->width = width;
    surface->height = height;
    surface->pitch = width * surface->bytes_per_pixel;
    surface->size = surface->pitch * height;
}

void d3d8_live_resolution_bind(D3DPRESENT_PARAMETERS *parameters,
                               D3D8Surface *backbuffer,
                               D3D8Surface *depth,
                               D3D8State *state)
{
    g_active.parameters = parameters;
    g_active.backbuffer = backbuffer;
    g_active.depth = depth;
    g_active.state = state;
}

void d3d8_live_resolution_unbind(void)
{
    g_active = (ActivePresentation){0};
}

int d3d8_live_resolution_apply(uint32_t width, uint32_t height,
                               char *why, int whyn)
{
    D3DPRESENT_PARAMETERS *parameters = g_active.parameters;
    D3D8Surface *backbuffer = g_active.backbuffer;
    D3D8Surface *depth = g_active.depth;
    D3D8State *state = g_active.state;

    if (!parameters || !backbuffer || !state)
        return fail(why, whyn, "no active D3D8 backbuffer");
    if (!width || !height || width > MAX_RESOLUTION_DIMENSION ||
        height > MAX_RESOLUTION_DIMENSION)
        return fail(why, whyn, "resolution is outside the supported range");
    if (backbuffer->kind != D3D8_SURF_BACKBUFFER ||
        !backbuffer->bytes_per_pixel ||
        (depth && (depth->kind != D3D8_SURF_DEPTHSTENCIL ||
                   !depth->bytes_per_pixel)))
        return fail(why, whyn, "active D3D8 surfaces are inconsistent");
    if (gpu_frame_in_progress())
        return fail(why, whyn, "cannot resize during an open D3D8 frame");
    if (!gpu_device_set_backbuffer_size(width, height))
        return fail(why, whyn, "host GPU refused the logical backbuffer size");

    parameters->BackBufferWidth = width;
    parameters->BackBufferHeight = height;
    resize_surface(backbuffer, width, height);
    resize_surface(depth, width, height);

    /* D3D8 Reset establishes a full-backbuffer viewport. Preserve all other
       live device state because this port-owned transition does not ask the
       retained engine to rebuild it. */
    state->viewport_x = 0;
    state->viewport_y = 0;
    state->viewport_w = (int32_t)width;
    state->viewport_h = (int32_t)height;
    state->viewport_minz = 0.0f;
    state->viewport_maxz = 1.0f;
    state->viewport_set = 1;
    gpu_frame_viewport(0, 0, (int)width, (int)height, 0.0f, 1.0f);

    if (why && whyn > 0)
        snprintf(why, (size_t)whyn, "game backbuffer is now %ux%u",
                 width, height);
    return 1;
}
