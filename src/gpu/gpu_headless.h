#ifndef GPU_HEADLESS_H
#define GPU_HEADLESS_H

/*
 * The off-screen target a windowless run renders into, and reading it back.
 *
 * Split from gpu_device.c because it is a different question: that file owns
 * the frame -- acquire, clear, draw, present -- while this owns WHERE the
 * frame goes when there is no swapchain, and how a harness gets the pixels
 * out. The frame path asks whether headless is active rather than reading the
 * state, so there is one owner of "is there a window" instead of two.
 */
#include <stdint.h>

#ifdef X2_WITH_SDL
struct SDL_GPUTexture;
/* The target, made on first use: the GPU device does not exist until the
   guest asks for one. NULL means it could not be made, and that has been
   reported by name. */
struct SDL_GPUTexture *gpu_headless_target(void);
#endif

int      gpu_headless_active(void);
uint32_t gpu_headless_width(void);
uint32_t gpu_headless_height(void);
unsigned long gpu_headless_frames(void);
void     gpu_headless_note_frame(void);

/*
 * Follow a backbuffer resize, unless the harness gave an explicit size.
 *
 * Port Settings is a windowed RmlUi path, so a headless run has no settings
 * document to follow; this only keeps the zero-argument diagnostics working.
 */
void gpu_headless_follow_backbuffer(uint32_t width, uint32_t height);

#endif /* GPU_HEADLESS_H */
