/*
 * The host GPU device and the frame, on SDL3's GPU API (Vulkan on Linux).
 *
 * See igvk_device.h for why this knows nothing about the guest.
 *
 * ## The frame, and why the clear is deferred
 *
 * igDxVisualContext drives a D3D8-shaped frame: BeginScene, then any number of
 * Clear/SetViewport/draw calls, then EndScene and Present. In D3D8 a Clear is
 * a command you issue inside the scene. In SDL_GPU -- and in Vulkan beneath it
 * -- the clear is a property of BEGINNING a render pass, not a command inside
 * one.
 *
 * So the order the engine uses cannot be mapped call-for-call. What maps
 * exactly is: remember what the engine asked to clear, and open the render
 * pass lazily, at the first thing that actually needs a pass open. The engine
 * clears before it draws, which is the case this handles correctly and the
 * only case it claims to handle -- a clear issued after drawing has begun is
 * reported rather than dropped, because dropping it would show up as
 * smearing between frames and be attributed to anything but this.
 */
#include "igvk_device.h"
#include "win32_sdl.h"

#include <stdio.h>
#include <string.h>

#ifdef X2_WITH_SDL
#include <SDL3/SDL.h>

static SDL_GPUDevice     *g_gpu;
static SDL_Window        *g_win;          /* swapchain claimed on this */
static SDL_GPUCommandBuffer *g_cmd;
static SDL_GPUTexture    *g_swap;
static SDL_GPURenderPass *g_pass;
static uint32_t           g_swap_w, g_swap_h;

/* What the next render pass must clear with. */
static struct {
    unsigned mask;
    float    r, g, b, a, depth;
    uint32_t stencil;
} g_clear;

static struct { int x, y, w, h; float minz, maxz; int set; } g_viewport;

/* Counters. A black screen is ambiguous; these disambiguate it. */
static unsigned long g_frames_presented, g_frames_no_swapchain,
                     g_frames_no_window, g_late_clears;
#endif

int igvk_device_create(void)
{
#ifndef X2_WITH_SDL
    fprintf(stderr, "igVk: built without SDL; no GPU device can be made.\n");
    return 0;
#else
    if (g_gpu) return 1;
    if (!SDL_WasInit(SDL_INIT_VIDEO) && !SDL_Init(SDL_INIT_VIDEO))
        fprintf(stderr, "igVk: SDL_Init(VIDEO) failed: %s\n", SDL_GetError());

    g_gpu = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, true, NULL);
    if (!g_gpu) {
        fprintf(stderr,
                "igVk: SDL_CreateGPUDevice(SPIRV) FAILED: %s\n"
                "  No GPU device means nothing can be drawn. Reported here "
                "rather than later as a blank frame.\n", SDL_GetError());
        return 0;
    }
    printf("igVk: GPU device created -- backend \"%s\"\n",
           SDL_GetGPUDeviceDriver(g_gpu));
    fflush(stdout);

    /* Attach immediately if the guest has already made its window. It usually
       has: CreateWindowExA runs well before the renderer is instantiated.
       setNativeWindowHandle re-attaches if that order ever changes. */
    igvk_device_attach_window(win32_sdl_window());
    return 1;
#endif
}

void igvk_device_destroy(void)
{
#ifdef X2_WITH_SDL
    if (!g_gpu) return;
    if (g_win) SDL_ReleaseWindowFromGPUDevice(g_gpu, g_win);
    SDL_DestroyGPUDevice(g_gpu);
    g_gpu = NULL;
    g_win = NULL;
#endif
}

int igvk_device_ready(void)
{
#ifdef X2_WITH_SDL
    return g_gpu != NULL;
#else
    return 0;
#endif
}

int igvk_device_attach_window(struct SDL_Window *w)
{
#ifndef X2_WITH_SDL
    (void)w;
    return 0;
#else
    SDL_Window *win = (SDL_Window *)w;
    if (!g_gpu) return 0;
    if (win == g_win) return win != NULL;
    if (g_win) {
        SDL_ReleaseWindowFromGPUDevice(g_gpu, g_win);
        g_win = NULL;
    }
    if (!win) return 0;
    if (!SDL_ClaimWindowForGPUDevice(g_gpu, win)) {
        fprintf(stderr, "igVk: SDL_ClaimWindowForGPUDevice failed: %s\n"
                        "  There is a window and a device but no swapchain "
                        "between them, so nothing can reach the screen.\n",
                SDL_GetError());
        return 0;
    }
    g_win = win;
    printf("igVk: swapchain claimed on window %p\n", (void *)win);
    fflush(stdout);
    return 1;
#endif
}

#ifdef X2_WITH_SDL
/*
 * Set the viewport, clamped to the swapchain.
 *
 * The engine clamps too -- against its current render destination -- but the
 * clamped rectangle only ever exists in its stack frame, so the slot hands us
 * the REQUEST and the clamp is redone here against the thing actually being
 * drawn into. Vulkan rejects a viewport outside the attachment, so this is a
 * requirement and not a nicety.
 *
 * Nothing to clamp against before the swapchain texture is acquired, so this
 * is called from pass_begin rather than from the setter.
 */
static void apply_viewport(void)
{
    SDL_GPUViewport vp;
    int x, y, w, h;

    if (!g_pass) return;
    if (!g_viewport.set) {
        /* No setViewport yet: the whole target, which is what a freshly
           created D3D device would also have. */
        vp.x = 0.0f; vp.y = 0.0f;
        vp.w = (float)g_swap_w; vp.h = (float)g_swap_h;
        vp.min_depth = 0.0f; vp.max_depth = 1.0f;
        SDL_SetGPUViewport(g_pass, &vp);
        return;
    }
    x = g_viewport.x < 0 ? 0 : g_viewport.x;
    y = g_viewport.y < 0 ? 0 : g_viewport.y;
    if (x > (int)g_swap_w) x = (int)g_swap_w;
    if (y > (int)g_swap_h) y = (int)g_swap_h;
    w = g_viewport.w;
    h = g_viewport.h;
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    if (x + w > (int)g_swap_w) w = (int)g_swap_w - x;
    if (y + h > (int)g_swap_h) h = (int)g_swap_h - y;
    if (w < 1 || h < 1) {
        static int told;
        if (!told++)
            fprintf(stderr, "igVk: the requested viewport %dx%d at (%d,%d) "
                            "does not intersect the %ux%u target; leaving the "
                            "previous one.\n", g_viewport.w, g_viewport.h,
                    g_viewport.x, g_viewport.y, g_swap_w, g_swap_h);
        return;
    }
    vp.x = (float)x; vp.y = (float)y;
    vp.w = (float)w; vp.h = (float)h;
    vp.min_depth = g_viewport.minz;
    vp.max_depth = g_viewport.maxz;
    SDL_SetGPUViewport(g_pass, &vp);
}

/*
 * Open the render pass, clearing as the engine asked.
 *
 * Everything that draws goes through here first. Called at most once per
 * frame; the pass stays open until igvk_frame_end.
 */
static void pass_begin(void)
{
    SDL_GPUColorTargetInfo ct;

    if (g_pass || !g_cmd || !g_swap) return;
    memset(&ct, 0, sizeof ct);
    ct.texture = g_swap;
    ct.clear_color.r = g_clear.r;
    ct.clear_color.g = g_clear.g;
    ct.clear_color.b = g_clear.b;
    ct.clear_color.a = g_clear.a;
    /*
     * DONT_CARE, not LOAD, when the engine did not ask for a colour clear.
     *
     * The swapchain texture's previous contents belong to a frame that has
     * already been presented and may be any buffer in the chain, so LOAD
     * would preserve something arbitrary rather than "the last frame".
     * DONT_CARE says truthfully that nothing is being preserved.
     */
    ct.load_op = (g_clear.mask & 1u) ? SDL_GPU_LOADOP_CLEAR
                                     : SDL_GPU_LOADOP_DONT_CARE;
    ct.store_op = SDL_GPU_STOREOP_STORE;

    g_pass = SDL_BeginGPURenderPass(g_cmd, &ct, 1, NULL);
    if (!g_pass) {
        fprintf(stderr, "igVk: SDL_BeginGPURenderPass failed: %s\n",
                SDL_GetError());
        return;
    }
    apply_viewport();
}
#endif

int igvk_frame_begin(void)
{
#ifndef X2_WITH_SDL
    return 0;
#else
    static int told_no_window;

    if (!g_gpu) return 0;
    if (!g_win) {
        /* Re-try the attach: the guest may have created its window after the
           renderer was instantiated. */
        if (!igvk_device_attach_window(win32_sdl_window())) {
            g_frames_no_window++;
            if (!told_no_window++)
                printf("igVk: frames are being driven with no window to "
                       "present to (--no-window, or the guest made none). "
                       "The frame loop runs; nothing reaches a screen.\n");
            return 0;
        }
    }
    if (g_cmd) {
        fprintf(stderr, "igVk: beginDraw while a frame is already open -- the "
                        "previous one was never ended. Ending it.\n");
        igvk_frame_end();
    }
    g_cmd = SDL_AcquireGPUCommandBuffer(g_gpu);
    if (!g_cmd) {
        fprintf(stderr, "igVk: SDL_AcquireGPUCommandBuffer failed: %s\n",
                SDL_GetError());
        return 0;
    }
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(g_cmd, g_win, &g_swap,
                                               &g_swap_w, &g_swap_h)) {
        fprintf(stderr, "igVk: acquiring the swapchain texture failed: %s\n",
                SDL_GetError());
        SDL_CancelGPUCommandBuffer(g_cmd);
        g_cmd = NULL;
        return 0;
    }
    if (!g_swap) {
        /* Legitimate and transient -- the window is minimised, or every image
           is still in flight. Counted so a run that NEVER gets one is
           distinguishable from one that occasionally misses. */
        SDL_CancelGPUCommandBuffer(g_cmd);
        g_cmd = NULL;
        g_frames_no_swapchain++;
        return 0;
    }
    g_clear.mask = 0;
    return 1;
#endif
}

void igvk_frame_end(void)
{
#ifdef X2_WITH_SDL
    if (!g_cmd) return;
    /*
     * Open the pass even if nothing drew.
     *
     * A frame in which the engine only cleared is a real frame -- it is what
     * the port produces before any geometry works -- and without this the
     * clear would never be executed and the window would stay whatever the
     * compositor left in it.
     */
    pass_begin();
    if (g_pass) {
        SDL_EndGPURenderPass(g_pass);
        g_pass = NULL;
    }
    SDL_SubmitGPUCommandBuffer(g_cmd);
    g_cmd = NULL;
    g_swap = NULL;
    g_frames_presented++;
#endif
}

int igvk_frame_in_progress(void)
{
#ifdef X2_WITH_SDL
    return g_cmd != NULL;
#else
    return 0;
#endif
}

void igvk_frame_clear(unsigned mask, float r, float g, float b, float a,
                      float depth, uint32_t stencil)
{
#ifndef X2_WITH_SDL
    (void)mask; (void)r; (void)g; (void)b; (void)a; (void)depth; (void)stencil;
#else
    if (!g_cmd) return;              /* outside a frame; nothing to clear */
    if (g_pass) {
        /* See the header comment: SDL_GPU cannot clear inside an open pass.
           Reported, not dropped -- see the file comment for why. */
        if (!g_late_clears++)
            fprintf(stderr,
                    "igVk: clearRenderDestination arrived AFTER the render "
                    "pass was opened, and SDL_GPU clears only on pass entry, "
                    "so this clear did NOT happen. Reported once; the total "
                    "is in the shutdown report.\n");
        return;
    }
    g_clear.mask = mask;
    g_clear.r = r; g_clear.g = g; g_clear.b = b; g_clear.a = a;
    g_clear.depth = depth;
    g_clear.stencil = stencil;
#endif
}

void igvk_frame_viewport(int x, int y, int w, int h, float minz, float maxz)
{
#ifndef X2_WITH_SDL
    (void)x; (void)y; (void)w; (void)h; (void)minz; (void)maxz;
#else
    g_viewport.x = x; g_viewport.y = y;
    g_viewport.w = w; g_viewport.h = h;
    g_viewport.minz = minz; g_viewport.maxz = maxz;
    g_viewport.set = 1;
    apply_viewport();               /* no-op until a pass is open */
#endif
}

void igvk_device_report(void)
{
#ifdef X2_WITH_SDL
    if (!g_gpu && !g_frames_no_window) return;
    printf("igVk: %lu frame(s) presented, %lu skipped for no swapchain "
           "texture, %lu with no window, %lu clear(s) lost to a pass that was "
           "already open\n",
           g_frames_presented, g_frames_no_swapchain, g_frames_no_window,
           g_late_clears);
    fflush(stdout);
#endif
}
