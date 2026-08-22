/*
 * The host GPU device and the frame, on SDL3's GPU API (Vulkan on Linux).
 *
 * See gpu_device.h for why this knows nothing about the guest.
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
#include "gpu_device.h"
#include "gpu_capture.h"
#include "gpu_draw.h"
#include "gpu_frame_timing.h"
#include "gpu_frame_submit.h"
#include "gpu_internal.h"
#include "gpu_present.h"
#include "gpu_shadow.h"
#include "rmlui_ui.h"
#include "settings_store.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef X2_WITH_SDL
#include <SDL3/SDL.h>

SDL_GPUDevice        *g_gpu;
static SDL_Window    *g_win;              /* swapchain claimed on this */
SDL_GPUCommandBuffer *g_cmd;
SDL_GPUTexture       *g_swap;
SDL_GPURenderPass    *g_pass;
uint32_t              g_swap_w, g_swap_h;
static SDL_GPUTexture *g_output;
static uint32_t        g_output_w, g_output_h;
/* Where the frame goes when it is not going to the swapchain: the self-test
   and, later, the engine's off-screen render destinations. */
static SDL_GPUTexture *g_offscreen;

static SDL_GPUTexture *g_depth_tex;
static SDL_GPUTextureFormat g_depth_fmt;
static uint32_t g_depth_w, g_depth_h;

/* Headless: no window, and the frame renders into this instead. */
static int             g_headless;
static uint32_t        g_headless_w = 800, g_headless_h = 600;
static int             g_headless_size_explicit;
static SDL_GPUTexture *g_headless_tex;
static unsigned long   g_headless_frames;
static SDL_Window *(*g_window_provider)(void);

/* What the next render pass must clear with. */
static struct {
    unsigned mask;
    float    r, g, b, a, depth;
    uint32_t stencil;
} g_clear;

static struct { int x, y, w, h; float minz, maxz; int set; } g_viewport;

/* Counters. A black screen is ambiguous; these disambiguate it. */
/* Set when X2_MAX_FRAMES is reached. READ by the host's heartbeat thread,
   which is where the reports live -- this file does not know that thread
   exists, and keeping it that way is the point of the file comment above. */
static int g_frame_limit_hit;
static unsigned long g_frames_presented, g_frames_no_swapchain,
                     g_frames_no_window, g_late_clears;

/*
 * The frame-phase profiler's device side.
 *
 * Frame WALL time measured present-to-present at gpu_frame_end. The guest is
 * recompiled C on the same thread, so one interval to the next is the whole
 * cost of a frame -- guest crossings plus the draw/upload paths timed in
 * gpu_draw.c plus this submit -- and subtracting the two known parts from the
 * interval is what the guest share is left over as. The histogram is the
 * shape, not just the mean: a constant 15 fps and a 60 fps that dips for one
 * frame out of twenty have the same average, and one of those is ""free"" in
 * a way the heartbeat's deltas gloss over.
 *
 * Reads are the heartbeat's usual torn-read trade, stated once in heartbeat.c.
 */
static unsigned long long g_frame_ns, g_frame_ns_min, g_frame_ns_max;
static unsigned long long g_last_frame_end_ns, g_frame_end_submits;
static unsigned long g_frame_intervals;  /* intervals folded into g_frame_ns */
static unsigned long g_frame_hist[GPU_FRAME_HISTOGRAM_BUCKETS];
#endif

int gpu_device_create(void)
{
#ifndef X2_WITH_SDL
    fprintf(stderr, "gpu: built without SDL; no GPU device can be made.\n");
    return 0;
#else
    X2Settings *settings = x2_settings_store();
    gpu_shadow_configure(settings->dynamic_shadows,
                         settings->shadow_resolution);
    if (g_gpu) return 1;
    if (!SDL_WasInit(SDL_INIT_VIDEO) && !SDL_Init(SDL_INIT_VIDEO))
        fprintf(stderr, "gpu: SDL_Init(VIDEO) failed: %s\n", SDL_GetError());

    /*
     * The Vulkan VALIDATION LAYER, and whether it is loaded, said out loud.
     *
     * This was hardcoded true. SDL's debug_mode loads
     * libVkLayer_khronos_validation.so, which inspects every draw, every bind
     * and every upload -- and this backend submits half a million draws in a
     * driven run. A backtrace of a live run showed the layer's own queue
     * thread in the process, so the tax was being paid and nothing said so:
     * "the native version is laggy" was measured against a build with a
     * per-draw validator in it.
     *
     * Off by default, on with X2_GPU_DEBUG=1, and ANNOUNCED either way --
     * because the thing that made this expensive to find was not that it was
     * on, it was that no line of output distinguished the two builds.
     */
    {
        const char *e = getenv("X2_GPU_DEBUG");
        int debug = e && *e && *e != '0';
        printf("gpu: Vulkan validation is %s (X2_GPU_DEBUG=%s). It inspects "
               "EVERY draw; a timing measured with it on is not a timing of "
               "this renderer.\n",
               debug ? "ON" : "off", e && *e ? e : "unset");
        fflush(stdout);
        g_gpu = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, debug, NULL);
    }
    if (!g_gpu) {
        fprintf(stderr,
                "gpu: SDL_CreateGPUDevice(SPIRV) FAILED: %s\n"
                "  No GPU device means nothing can be drawn. Reported here "
                "rather than later as a blank frame.\n", SDL_GetError());
        return 0;
    }
    printf("gpu: GPU device created -- backend \"%s\"\n",
           SDL_GetGPUDeviceDriver(g_gpu));
    fflush(stdout);

    /* Attach immediately if a window already exists -- under x2native the
       guest's CreateWindowExA usually runs well before the renderer is
       instantiated. A front-end that attaches its own window installs no
       provider and simply does it itself. */
    if (g_window_provider) gpu_device_attach_window(g_window_provider());
    return 1;
#endif
}

void gpu_device_destroy(void)
{
#ifdef X2_WITH_SDL
    /* RmlUi owns pipelines, buffers and textures on this device. Its backend
       must release them before the device and before the claimed window. */
    x2_ui_gpu_shutdown();
    /* Buffers, textures and pipelines belong to the device, so they go before
       it does. Releasing them after SDL_DestroyGPUDevice is a use-after-free
       that only shows up under a validation layer. */
    gpu_draw_shutdown();
    gpu_present_shutdown(g_gpu);
#endif
#ifdef X2_WITH_SDL
    if (!g_gpu) return;
    if (g_depth_tex) SDL_ReleaseGPUTexture(g_gpu, g_depth_tex);
    g_depth_tex = NULL;
    g_depth_w = g_depth_h = 0;
    g_depth_fmt = SDL_GPU_TEXTUREFORMAT_INVALID;
    if (g_win) SDL_ReleaseWindowFromGPUDevice(g_gpu, g_win);
    SDL_DestroyGPUDevice(g_gpu);
    g_gpu = NULL;
    g_win = NULL;
#endif
}

int gpu_device_ready(void)
{
#ifdef X2_WITH_SDL
    return g_gpu != NULL;
#else
    return 0;
#endif
}

int gpu_device_set_backbuffer_size(uint32_t width, uint32_t height)
{
#ifdef X2_WITH_SDL
    if (!gpu_present_set_scene_size(width, height)) return 0;
    if (g_headless && !g_headless_size_explicit) {
        if (g_headless_tex) {
            SDL_ReleaseGPUTexture(g_gpu, g_headless_tex);
            g_headless_tex = NULL;
        }
        g_headless_w = width;
        g_headless_h = height;
    }
    return 1;
#else
    (void)width;
    (void)height;
    return 0;
#endif
}

void gpu_device_set_window_provider(struct SDL_Window *(*fn)(void))
{
#ifdef X2_WITH_SDL
    g_window_provider = fn;
#else
    (void)fn;
#endif
}

int gpu_device_attach_window(struct SDL_Window *w)
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
        fprintf(stderr, "gpu: SDL_ClaimWindowForGPUDevice failed: %s\n"
                        "  There is a window and a device but no swapchain "
                        "between them, so nothing can reach the screen.\n",
                SDL_GetError());
        return 0;
    }
    g_win = win;
    printf("gpu: swapchain claimed on window %p\n", (void *)win);
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
            fprintf(stderr, "gpu: the requested viewport %dx%d at (%d,%d) "
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
 * frame; the pass stays open until gpu_frame_end.
 */
void gpu_set_offscreen_target(SDL_GPUTexture *t, uint32_t w, uint32_t h)
{
    g_offscreen = t;
    if (t) { g_swap = t; g_swap_w = w; g_swap_h = h; }
}

/*
 * The depth/stencil buffer.
 *
 * D3D8 gives the device an automatic depth surface (the game asks for
 * D3DFMT_D16 via depth=auto) and then depth-tests every piece of world
 * geometry against it. Without one, 476,531 draws in a menu run asked for a
 * depth test that could not happen and drew in submission order instead --
 * which is not a subtle difference: it is what makes the far wall paint over
 * the character standing in front of it.
 *
 * The format is CHOSEN by asking the driver, not assumed: D24_UNORM_S8_UINT is
 * the usual one but is not universal, so the alternatives are tried in turn
 * and the failure to find any is reported by name rather than left to show up
 * as a pass that will not begin.
 */
/*
 * The format is resolved on FIRST ASK, not on first pass.
 *
 * A pipeline is built before the pass it will draw into is opened, and it has
 * to declare the same depth format the pass attaches -- so if the format were
 * only decided when the pass opened, the first pipeline of the run would
 * declare "no depth" and then be bound against a pass that has one. That is a
 * validation error, and it would have been the FIRST draw of every run.
 */
unsigned long gpu_frames_presented(void) { return g_frames_presented; }
int gpu_frame_limit_reached(void) { return g_frame_limit_hit; }

/*
 * The frame-phase profiler's device side, for the heartbeat.
 *
 * The draw side comes from gpu_draw_perf(); the two are read together by the
 * heartbeat, which is where subtraction of the known host paths from the
 * frame interval leaves the guest's share.
 */
void gpu_device_perf(unsigned long long *frame_ns,
                     unsigned long long *frame_ns_min,
                     unsigned long long *frame_ns_max,
                     unsigned long long *end_submits,
                     unsigned long *intervals,
                     const unsigned long **hist)
{
    *frame_ns = g_frame_ns;
    *frame_ns_min = g_frame_ns_min;
    *frame_ns_max = g_frame_ns_max;
    *end_submits = g_frame_end_submits;
    *intervals = g_frame_intervals;
    *hist = g_frame_hist;
}

SDL_GPUTextureFormat gpu_depth_format(void)
{
    static const SDL_GPUTextureFormat WANT[] = {
        SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT,
        SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT,
        SDL_GPU_TEXTUREFORMAT_D24_UNORM,
        SDL_GPU_TEXTUREFORMAT_D32_FLOAT,
        SDL_GPU_TEXTUREFORMAT_D16_UNORM,
    };
    unsigned i;

    if (g_depth_fmt != SDL_GPU_TEXTUREFORMAT_INVALID) return g_depth_fmt;
    if (!g_gpu) return SDL_GPU_TEXTUREFORMAT_INVALID;
    for (i = 0; i < sizeof WANT / sizeof WANT[0]; i++) {
        if (SDL_GPUTextureSupportsFormat(g_gpu, WANT[i],
                                         SDL_GPU_TEXTURETYPE_2D,
                                         SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET)) {
            g_depth_fmt = WANT[i];
            return g_depth_fmt;
        }
    }
    {
        static int told;
        if (!told++)
            fprintf(stderr, "gpu: this device supports NONE of the five depth "
                            "formats asked for, so there is no depth buffer "
                            "and everything draws in submission order.\n");
    }
    return SDL_GPU_TEXTUREFORMAT_INVALID;
}

SDL_GPUTexture *gpu_depth_target(uint32_t w, uint32_t h)
{
    SDL_GPUTextureCreateInfo ci;

    if (!g_gpu || !w || !h) return NULL;
    if (g_depth_tex && g_depth_w == w && g_depth_h == h) return g_depth_tex;
    if (gpu_depth_format() == SDL_GPU_TEXTUREFORMAT_INVALID) return NULL;
    if (g_depth_tex) {
        SDL_ReleaseGPUTexture(g_gpu, g_depth_tex);
        g_depth_tex = NULL;
    }
    memset(&ci, 0, sizeof ci);
    ci.type = SDL_GPU_TEXTURETYPE_2D;
    ci.format = g_depth_fmt;
    ci.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
    ci.width = w;
    ci.height = h;
    ci.layer_count_or_depth = 1;
    ci.num_levels = 1;
    g_depth_tex = SDL_CreateGPUTexture(g_gpu, &ci);
    if (!g_depth_tex) {
        fprintf(stderr, "gpu: the %ux%u depth target could not be made: %s -- "
                        "everything will draw in submission order.\n",
                w, h, SDL_GetError());
        return NULL;
    }
    g_depth_w = w;
    g_depth_h = h;
    return g_depth_tex;
}

/*
 * `reopen` is a pass being started again in the MIDDLE of a frame, because a
 * clear arrived after drawing had begun. The difference is what happens to the
 * attachments that are NOT being cleared: at frame start nothing is worth
 * preserving, mid-frame everything drawn so far is.
 */
static void pass_begin(int reopen)
{
    SDL_GPUColorTargetInfo ct;
    SDL_GPUDepthStencilTargetInfo dt;
    SDL_GPUTexture *depth;

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
               : reopen                ? SDL_GPU_LOADOP_LOAD
                                       : SDL_GPU_LOADOP_DONT_CARE;
    ct.store_op = SDL_GPU_STOREOP_STORE;

    depth = gpu_depth_target(g_swap_w, g_swap_h);
    memset(&dt, 0, sizeof dt);
    if (depth) {
        dt.texture = depth;
        /*
         * CLEAR when the engine asked, and CLEAR when it did not.
         *
         * The contents from the previous frame are meaningless to this one and
         * LOADing them would depth-test against the last frame's geometry.
         * D3D8's own semantics are that a frame that draws depth-tested
         * geometry clears Z first; a frame that forgets is drawing against
         * garbage on real hardware too, and 1.0 is the value that lets
         * everything through rather than a value chosen to hide the mistake.
         */
        dt.clear_depth = (g_clear.mask & 2u) ? g_clear.depth : 1.0f;
        dt.clear_stencil = (Uint8)((g_clear.mask & 4u) ? g_clear.stencil : 0u);
        /* Mid-frame, only what the engine ASKED to clear is cleared: a depth
           clear before the HUD must not throw away the colour, and a colour
           clear must not throw away the depth. */
        dt.load_op = (!reopen || (g_clear.mask & 2u)) ? SDL_GPU_LOADOP_CLEAR
                                                      : SDL_GPU_LOADOP_LOAD;
        dt.store_op = SDL_GPU_STOREOP_STORE;
        dt.stencil_load_op = (!reopen || (g_clear.mask & 4u))
                                 ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
        dt.stencil_store_op = SDL_GPU_STOREOP_STORE;
    }

    g_pass = SDL_BeginGPURenderPass(g_cmd, &ct, 1, depth ? &dt : NULL);
    if (!g_pass) {
        fprintf(stderr, "gpu: SDL_BeginGPURenderPass failed: %s\n",
                SDL_GetError());
        return;
    }
    apply_viewport();
}

void gpu_pass_begin(void) { pass_begin(0); }
#endif

void gpu_device_headless(int on, uint32_t w, uint32_t h)
{
    g_headless = on;
    if (w && h) {
        g_headless_w = w;
        g_headless_h = h;
        g_headless_size_explicit = 1;
    }
    if (on)
        printf("gpu: HEADLESS -- frames render into an off-screen %ux%u target; "
               "there is no window and nothing is presented to a screen.\n",
               g_headless_w, g_headless_h);
}

#ifdef X2_WITH_SDL
/* Made on first use, because the GPU device does not exist until the guest
   asks for one. */
static SDL_GPUTexture *headless_target(void)
{
    SDL_GPUTextureCreateInfo ci;
    if (g_headless_tex) return g_headless_tex;
    memset(&ci, 0, sizeof ci);
    ci.type = SDL_GPU_TEXTURETYPE_2D;
    ci.format = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
    ci.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    ci.width = g_headless_w;
    ci.height = g_headless_h;
    ci.layer_count_or_depth = 1;
    ci.num_levels = 1;
    g_headless_tex = SDL_CreateGPUTexture(g_gpu, &ci);
    if (!g_headless_tex)
        fprintf(stderr, "gpu: the headless target (%ux%u) could not be made: "
                        "%s -- this run will draw NOTHING.\n",
                g_headless_w, g_headless_h, SDL_GetError());
    return g_headless_tex;
}
#endif

int gpu_frame_begin(void)
{
#ifndef X2_WITH_SDL
    return 0;
#else
    static int told_no_window;

    if (!g_gpu) return 0;
    if (g_headless) {
        SDL_GPUTexture *t = headless_target();
        if (!t) return 0;
        if (g_cmd) gpu_frame_end();
        g_cmd = SDL_AcquireGPUCommandBuffer(g_gpu);
        if (!g_cmd) {
            fprintf(stderr, "gpu: SDL_AcquireGPUCommandBuffer failed: %s\n",
                    SDL_GetError());
            return 0;
        }
        gpu_set_offscreen_target(t, g_headless_w, g_headless_h);
        gpu_shadow_frame_begin();
        g_clear.mask = 0;
        gpu_frame_host_reset();
        g_headless_frames++;
        return 1;
    }
    if (!g_win) {
        /* Re-try the attach: the guest may have created its window after the
           renderer was instantiated. */
        if (!g_window_provider ||
            !gpu_device_attach_window(g_window_provider())) {
            g_frames_no_window++;
            if (!told_no_window++)
                printf("gpu: frames are being driven with no window to "
                       "present to (--no-window, or the guest made none). "
                       "The frame loop runs; nothing reaches a screen.\n");
            return 0;
        }
    }
    if (g_cmd) {
        fprintf(stderr, "gpu: beginDraw while a frame is already open -- the "
                        "previous one was never ended. Ending it.\n");
        gpu_frame_end();
    }
    g_cmd = SDL_AcquireGPUCommandBuffer(g_gpu);
    if (!g_cmd) {
        fprintf(stderr, "gpu: SDL_AcquireGPUCommandBuffer failed: %s\n",
                SDL_GetError());
        return 0;
    }
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(g_cmd, g_win, &g_output,
                                               &g_output_w, &g_output_h)) {
        fprintf(stderr, "gpu: acquiring the swapchain texture failed: %s\n",
                SDL_GetError());
        SDL_CancelGPUCommandBuffer(g_cmd);
        g_cmd = NULL;
        return 0;
    }
    if (!g_output) {
        /* Legitimate and transient -- the window is minimised, or every image
           is still in flight. Counted so a run that NEVER gets one is
           distinguishable from one that occasionally misses. */
        SDL_CancelGPUCommandBuffer(g_cmd);
        g_cmd = NULL;
        g_frames_no_swapchain++;
        return 0;
    }
    g_swap = g_output;
    g_swap_w = g_output_w;
    g_swap_h = g_output_h;
    if (gpu_present_is_configured()) {
        g_swap = gpu_present_scene(g_gpu, &g_swap_w, &g_swap_h);
        if (!g_swap) {
            SDL_CancelGPUCommandBuffer(g_cmd);
            g_cmd = NULL;
            g_output = NULL;
            return 0;
        }
    }
    gpu_shadow_frame_begin();
    g_clear.mask = 0;
    gpu_frame_host_reset();
    return 1;
#endif
}

void gpu_frame_end(void)
{
#ifdef X2_WITH_SDL
    if (!g_cmd) return;
    gpu_shadow_frame_submit();
    {
        /* Present-to-present frame time, before this submit but after the
           previous one. The FIRST frame has no predecessor; it seeds the
           baseline rather than measuring a intervals-prior frame. */
        unsigned long long now = gpu_perf_now_ns();
        unsigned long long dt;
        if (g_last_frame_end_ns) {
            dt = now - g_last_frame_end_ns;
            g_frame_ns += dt;
            g_frame_intervals++;
            if (!g_frame_ns_min || dt < g_frame_ns_min) g_frame_ns_min = dt;
            if (dt > g_frame_ns_max) g_frame_ns_max = dt;
            g_frame_hist[gpu_frame_timing_bucket(dt)]++;
            /*
             * A frame slow enough to matter gets its cost attributed AT THE
             * MOMENT it ends, not in a shutdown report: the frame limiter
             * makes "slow" a wall-clock number and this is the only place the
             * wall clock and the host share of the same frame meet.
             *
             * The threshold is an ORDER OF MAGNITUDE above the paced frame
             * budget (60fps = 16.7ms), so a normal gameplay frame never
             * prints and a load stall does -- exactly the frames that want
             * asking "who was slow?". Said once per frame, since a load can
             * stall for several.
             */
            if (dt > 160000000ull) {
                unsigned long long dns, uns;
                gpu_frame_host_share(&dns, &uns);
                fprintf(stderr,
                        "gpu: frame %lu took %.0f ms; host draw %.1f ms + "
                        "upload %.1f ms, the rest is guest logic and the "
                        "submit\n",
                        g_frames_presented, (double)dt * 1e-6,
                        (double)dns * 1e-6, (double)uns * 1e-6);
            }
        }
        g_last_frame_end_ns = now;
    }
    /*
     * Open the pass even if nothing drew.
     *
     * A frame in which the engine only cleared is a real frame -- it is what
     * the port produces before any geometry works -- and without this the
     * clear would never be executed and the window would stay whatever the
     * compositor left in it.
     */
    gpu_pass_begin();
    if (g_pass) {
        SDL_EndGPURenderPass(g_pass);
        g_pass = NULL;
    }
    if (!g_offscreen && g_output && g_swap != g_output
        && !gpu_present_composite(g_cmd, g_output, g_output_w, g_output_h)) {
        SDL_CancelGPUCommandBuffer(g_cmd);
        g_cmd = NULL;
        g_swap = NULL;
        g_output = NULL;
        return;
    }
    if (!g_offscreen)
        x2_ui_render(g_gpu, g_cmd, g_output, g_output_w, g_output_h, g_win);
    gpu_frame_submit(g_gpu, g_cmd, g_headless);
    g_cmd = NULL;
    g_frame_end_submits++;
    if (!g_offscreen) {
        g_swap = NULL;
        g_output = NULL;
    }
    g_frames_presented++;
    gpu_capture_frame(g_headless, g_headless_frames,
                      g_headless_w, g_headless_h);
    /*
     * X2_MAX_FRAMES: stop cleanly after this many frames.
     *
     * This game never stops on its own, so every measured run has been ended
     * by a timeout -- which means every run costs its whole timeout even when
     * the thing being measured finished a minute earlier, and the reports come
     * out of a signal handler. Ending on the game's OWN frame counter makes a
     * run take as long as it needs and no longer, and makes two runs of the
     * same script the same length.
     *
     * It goes through the same path a SIGTERM does (the heartbeat thread does
     * the reports, because they are stdio), so nothing new has to be trusted.
     */
    {
        static long limit = -2;
        if (limit == -2) {
            const char *e = getenv("X2_MAX_FRAMES");
            limit = (e && *e) ? strtol(e, NULL, 0) : -1;
            if (limit > 0)
                fprintf(stderr, "gpu: X2_MAX_FRAMES=%ld -- the run will stop "
                                "cleanly after that many presented frames.\n",
                        limit);
        }
        /*
         * Said ONCE. The limit is a level, not an edge -- every frame after it
         * satisfies the test -- and without this guard the line repeated for as
         * long as the guest kept presenting. One run emitted 3,847 copies of it
         * and they were interleaved through the shutdown report, which is how a
         * report that HUNG midway looked like a report that had finished.
         * The frame number in the line is still the FIRST one over the limit,
         * which is the useful one; how far the guest ran after the stop was
         * requested is the stopping path's business to report, not this one's.
         */
        if (limit > 0 && (long)g_frames_presented >= limit
            && !g_frame_limit_hit) {
            fprintf(stderr, "\ngpu: X2_MAX_FRAMES reached (%lu presented). "
                            "Stopping; the reports follow.\n",
                    g_frames_presented);
            g_frame_limit_hit = 1;
        }
    }
#endif
}

int gpu_frame_in_progress(void)
{
#ifdef X2_WITH_SDL
    return g_cmd != NULL;
#else
    return 0;
#endif
}

void gpu_frame_clear(unsigned mask, float r, float g, float b, float a,
                      float depth, uint32_t stencil)
{
#ifndef X2_WITH_SDL
    (void)mask; (void)r; (void)g; (void)b; (void)a; (void)depth; (void)stencil;
#else
    if (!g_cmd) return;              /* outside a frame; nothing to clear */
    g_clear.mask = mask;
    g_clear.r = r; g_clear.g = g; g_clear.b = b; g_clear.a = a;
    g_clear.depth = depth;
    g_clear.stencil = stencil;
    if (g_pass) {
        /*
         * A clear arrived after drawing began. SDL_GPU clears only on pass
         * entry, so this used to be COUNTED AND DROPPED -- 3,833 of them in
         * one gameplay run, every one a clear the engine asked for and did not
         * get. Ending the pass and starting another with this clear as its
         * load op is what the API offers, and it is exactly the operation
         * D3D8's Clear names: the attachments the engine did not ask to clear
         * are LOADed, so nothing drawn so far is lost.
         */
        SDL_EndGPURenderPass(g_pass);
        g_pass = NULL;
        g_late_clears++;
        pass_begin(1);
    }
#endif
}

void gpu_frame_bind_target(unsigned index)
{
#ifndef X2_WITH_SDL
    (void)index;
#else
    static unsigned told_index = 0u - 1u;
    if (index == 0u) return;                 /* the back buffer: what we have */
    if (told_index != index) {
        told_index = index;
        fprintf(stderr,
                "gpu: the engine bound render destination %u, which is an "
                "OFF-SCREEN target this backend does not have -- there is only "
                "the swapchain. Drawing that follows goes to the back buffer "
                "instead of where the engine intends.\n", index);
    }
#endif
}

void gpu_frame_viewport(int x, int y, int w, int h, float minz, float maxz)
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

void gpu_device_report(void)
{
#ifdef X2_WITH_SDL
    if (!g_gpu && !g_frames_no_window) return;
    printf("gpu: %lu frame(s) presented, %lu skipped for no swapchain "
           "texture, %lu with no window, %lu clear(s) that arrived mid-frame "
           "and reopened the pass\n",
           g_frames_presented, g_frames_no_swapchain, g_frames_no_window,
           g_late_clears);
    fflush(stdout);
    if (g_frame_ns && g_frame_intervals) {
        double avg_ms = (double)g_frame_ns * 1e-6
                        / (double)g_frame_intervals;
        unsigned i;
        printf("  perf: frame wall avg %.2f ms, min %.2f ms, max %.2f ms, "
               "%llu submit(s) at frame end; ms histogram:\n",
               avg_ms, (double)g_frame_ns_min * 1e-6,
               (double)g_frame_ns_max * 1e-6,
               (unsigned long long)g_frame_end_submits);
        printf("        ");
        for (i = 0; i < GPU_FRAME_HISTOGRAM_BUCKETS; i++) {
            static const char *LBL[] = {
                "<1", "1-2", "2-4", "4-6", "6-10", "10-16", "16-25",
                "25-40", "40-60", "60-80", "80-120", "120-200", ">=200",
            };
            if (i) printf(" ");
            printf("%s=%lu", LBL[i], g_frame_hist[i]);
        }
        printf("\n");
        fflush(stdout);
    }
#endif
}


/*
 * The headless target's pixels.
 *
 * Deliberately its own path rather than gpu_offscreen_read(): that one owns
 * the off-screen texture the SELF-TEST makes and would tear down the live
 * frame. This reads the target the game has been rendering into, after
 * whatever frame last finished.
 */
/*
 * How big a readback would be, and whether one is possible at all.
 *
 * Separate from the read because the caller has to allocate before it can ask,
 * and folding the query into the read as "pass a null buffer" collides with
 * the buffer-size check -- the caller then cannot tell "too small" from "not
 * headless" from "no frame yet", which are three different answers.
 */
int gpu_device_headless_size(uint32_t *w_out, uint32_t *h_out, char *why,
                             int whyn)
{
    if (w_out) *w_out = 0;
    if (h_out) *h_out = 0;
#ifndef X2_WITH_SDL
    snprintf(why, (size_t)whyn, "this build has no SDL, so there is no "
                                "renderer to read a frame from.");
    return 0;
#else
    if (!g_headless || !g_headless_tex) {
        snprintf(why, (size_t)whyn,
                 "this run is not headless, so the frame goes to the screen "
                 "and there is no off-screen target to read. Launch with "
                 "--no-window.");
        return 0;
    }
    if (!g_headless_frames) {
        snprintf(why, (size_t)whyn,
                 "the %ux%u headless target exists but NO frame has been "
                 "rendered into it yet -- reading it would photograph an "
                 "uninitialised texture. The run is still starting up.",
                 g_headless_w, g_headless_h);
        return 0;
    }
    if (w_out) *w_out = g_headless_w;
    if (h_out) *h_out = g_headless_h;
    return 1;
#endif
}

int gpu_device_headless_read(void *bgra_out, uint32_t bytes,
                             uint32_t *w_out, uint32_t *h_out)
{
#ifndef X2_WITH_SDL
    (void)bgra_out; (void)bytes; (void)w_out; (void)h_out;
    fprintf(stderr, "gpu: no SDL in this build, so there is nothing to read.\n");
    return 0;
#else
    SDL_GPUTransferBufferCreateInfo tci;
    SDL_GPUTransferBuffer *tb;
    SDL_GPUCommandBuffer *cmd;
    SDL_GPUCopyPass *cp;
    SDL_GPUTextureRegion src;
    SDL_GPUTextureTransferInfo dst;
    SDL_GPUFence *fence;
    uint32_t need = g_headless_w * g_headless_h * 4u;
    void *p;

    if (!g_headless || !g_headless_tex) {
        fprintf(stderr, "gpu: this run is not headless (or no frame has been "
                        "rendered), so there is no target to read.\n");
        return 0;
    }
    if (!g_headless_frames) {
        fprintf(stderr, "gpu: the headless target exists but NO frame has been "
                        "rendered into it -- reading it would photograph an "
                        "uninitialised texture.\n");
        return 0;
    }
    if (bytes < need) {
        fprintf(stderr, "gpu: the readback needs %u bytes, was given %u.\n",
                need, bytes);
        return 0;
    }
    /* Whatever is in flight has to have executed before it can be read. */
    if (g_pass) { SDL_EndGPURenderPass(g_pass); g_pass = NULL; }
    if (g_cmd) {
        fence = SDL_SubmitGPUCommandBufferAndAcquireFence(g_cmd);
        g_cmd = NULL;
        if (fence) {
            SDL_WaitForGPUFences(g_gpu, true, &fence, 1);
            SDL_ReleaseGPUFence(g_gpu, fence);
        }
    }
    memset(&tci, 0, sizeof tci);
    tci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    tci.size = need;
    tb = SDL_CreateGPUTransferBuffer(g_gpu, &tci);
    if (!tb) { fprintf(stderr, "gpu: %s\n", SDL_GetError()); return 0; }
    cmd = SDL_AcquireGPUCommandBuffer(g_gpu);
    cp = SDL_BeginGPUCopyPass(cmd);
    memset(&src, 0, sizeof src);
    memset(&dst, 0, sizeof dst);
    src.texture = g_headless_tex;
    src.w = g_headless_w;
    src.h = g_headless_h;
    src.d = 1;
    dst.transfer_buffer = tb;
    SDL_DownloadFromGPUTexture(cp, &src, &dst);
    SDL_EndGPUCopyPass(cp);
    fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    if (fence) {
        SDL_WaitForGPUFences(g_gpu, true, &fence, 1);
        SDL_ReleaseGPUFence(g_gpu, fence);
    }
    p = SDL_MapGPUTransferBuffer(g_gpu, tb, false);
    if (!p) {
        fprintf(stderr, "gpu: mapping the readback failed: %s\n", SDL_GetError());
        SDL_ReleaseGPUTransferBuffer(g_gpu, tb);
        return 0;
    }
    memcpy(bgra_out, p, need);
    SDL_UnmapGPUTransferBuffer(g_gpu, tb);
    SDL_ReleaseGPUTransferBuffer(g_gpu, tb);
    if (w_out) *w_out = g_headless_w;
    if (h_out) *h_out = g_headless_h;
    return 1;
#endif
}
