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

#ifdef X2_WITH_SDL
#include <SDL3/SDL.h>

extern SDL_GPUDevice        *g_gpu;
extern SDL_GPUCommandBuffer *g_cmd;
extern SDL_GPURenderPass    *g_pass;
extern SDL_GPUTexture       *g_swap;
extern uint32_t              g_swap_w, g_swap_h;

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
#endif

#endif /* GPU_INTERNAL_H */
