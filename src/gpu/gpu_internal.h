/*
 * What the files inside src/gpu share, and nothing outside it may see.
 *
 * gpu_device.c owns the device, the swapchain and the frame; gpu_draw.c owns
 * resources and draws into that frame. They are one subsystem split by
 * concern, so the alternative to this header is accessor functions that exist
 * only to launder the same pointers -- and those hide, rather than document,
 * the fact that the two files share a frame.
 *
 * Nothing here is declared in gpu_device.h or gpu_draw.h on purpose: the
 * outside world sees "begin a frame, draw, present", not an SDL_GPUDevice.
 */
#ifndef GPU_INTERNAL_H
#define GPU_INTERNAL_H

#include <stdint.h>
#include <time.h>

/*
 * Wall clock, nanoseconds, monotonic.
 *
 * The only profiler primitive this subsystem needs. clock_gettime through the
 * vDSO is ~20-30 ns, which is why it can be called twice per draw and twice
 * per upload without perturbing the thing being measured to the point of
 * lying -- the repo's own reproof of the Vulkan validation layer is the other
 * side of the same trade: THAT instrument inspects every draw and changed the
 * timing, so it had to be off by default, and the ~40 ns of this one does not.
 * Said here so a slow frame is not chased through an instrument that caused
 * the slowness.
 */
static inline unsigned long long gpu_perf_now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long long)ts.tv_sec * 1000000000ull + ts.tv_nsec;
}

#ifdef X2_WITH_SDL
#include <SDL3/SDL.h>

extern SDL_GPUDevice        *g_gpu;
extern SDL_GPUCommandBuffer *g_cmd;
extern SDL_GPURenderPass    *g_pass;
extern SDL_GPUTexture       *g_swap;
extern uint32_t              g_swap_w, g_swap_h;

/*
 * The depth/stencil target the current pass renders against, and its format.
 *
 * The pipeline has to declare the SAME format the pass attaches, so gpu_draw.c
 * needs to know it -- and it must ask rather than assume, because which depth
 * format exists is a property of the driver (D24_UNORM_S8_UINT is not
 * universal). SDL_GPU_TEXTUREFORMAT_INVALID means there is no depth target,
 * which is the truthful answer before the device exists and the one a draw has
 * to be told rather than guess.
 */
SDL_GPUTextureFormat gpu_depth_format(void);

/* How many frames have been presented. gpu_draw.c uses it to know which frame
   a draw belongs to, for X2_FRAME_DUMP. */
unsigned long gpu_frames_presented(void);
SDL_GPUTexture      *gpu_depth_target(uint32_t w, uint32_t h);

/* Open the render pass if it is not open yet, clearing as the engine asked.
   Drawing needs the pass, and the pass has to be opened by whoever gets there
   first -- a draw or the end of the frame. */
void gpu_pass_begin(void);

/* Redirect the frame to an off-screen colour target instead of the swapchain,
   for gpu_offscreen_*. NULL restores the swapchain. */
void gpu_set_offscreen_target(SDL_GPUTexture *t, uint32_t w, uint32_t h);

/* Release every resource gpu_draw.c owns; called from gpu_device_destroy so
   the teardown order is the device's business, not a second lifetime. */
void gpu_draw_shutdown(void);
/* Draws this frame has received so far (before any X2_DRAW_RANGE skip). */
unsigned long gpu_frame_draws_so_far(void);

/*
 * The frame's HOST share so far (draw submission + uploads), for attributing a
 * slow frame at the moment it ends. gpu_frame_begin resets, gpu_frame_end
 * reads. Not in the public header on purpose: only the frame owner needs it.
 */
void gpu_frame_host_reset(void);
void gpu_frame_host_share(unsigned long long *draw_ns,
                          unsigned long long *upload_ns);

#endif

#endif /* GPU_INTERNAL_H */
