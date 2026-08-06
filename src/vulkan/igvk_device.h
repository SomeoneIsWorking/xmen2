/*
 * The host GPU device: SDL3's GPU API, which is Vulkan on Linux.
 *
 * Deliberately knows NOTHING about the guest -- no CPU state, no guest
 * addresses, no ARK. The slot modules unmarshal the engine's arguments and
 * call in here with plain C values. That split is the whole reason this file
 * exists separately: the guest-ABI grubbiness (who pops what, where the
 * hidden return pointer goes) stays at the boundary, and the renderer proper
 * is ordinary code that can be read, tested and reasoned about on its own.
 *
 * SDL types do not appear here on purpose. A caller needs "begin a frame",
 * "clear", "present" -- not an SDL_GPUDevice. Keeping SDL inside the .c means
 * the slot modules cannot start reaching around this interface.
 */
#ifndef IGVK_DEVICE_H
#define IGVK_DEVICE_H

#include <stdint.h>

/*
 * Create the GPU device. Returns 1 on success, 0 on failure.
 *
 * Failure is loud and immediate rather than deferred: a renderer with no
 * device produces a blank window, and a blank window is the single hardest
 * symptom to attribute. Called from the engine's own userInstantiate, so the
 * failure is reported at the moment the engine asked for a renderer.
 */
int igvk_device_create(void);
void igvk_device_destroy(void);

/* 1 once a device exists. This is what igDxVisualContext::getLastError's
   `this+0x144 == 0 -> error` test becomes for this backend. */
int igvk_device_ready(void);

/*
 * The window the swapchain presents to.
 *
 * The engine hands us a Win32 HWND through setNativeWindowHandle, which is
 * meaningless here; the host owns its own window and this is how the two are
 * connected. Passing NULL detaches. Returns 1 if the swapchain was claimed.
 */
struct SDL_Window;
int igvk_device_attach_window(struct SDL_Window *w);

/*
 * The frame.
 *
 * igDxVisualContext's frame boundary is BeginScene / EndScene+Present, driven
 * by slots 174 and 175. These are the same boundary: begin acquires the
 * swapchain texture and opens a command buffer, end submits it.
 *
 * igvk_frame_begin returns 0 when there is nothing to draw into (no device,
 * no window, or the swapchain had no texture ready this frame). A 0 must be
 * propagated -- the engine's beginDraw returns a bool for exactly this and
 * skips the frame when it is false.
 */
int  igvk_frame_begin(void);
void igvk_frame_end(void);
int  igvk_frame_in_progress(void);

/*
 * Clear the current render destination.
 *
 * `mask` is the engine's own flag byte from clearRenderDestination: bit 0
 * colour, bit 1 depth, bit 2 stencil. Colour components are 0..1.
 *
 * SDL_GPU clears as part of beginning a render pass rather than as a command,
 * so this records what the next pass must clear with. A clear requested after
 * the pass has already begun cannot be honoured that way and says so instead
 * of silently doing nothing.
 */
void igvk_frame_clear(unsigned mask, float r, float g, float b, float a,
                      float depth, uint32_t stencil);

/* Viewport in pixels, already clamped by the engine to the render
   destination. minz/maxz are the depth range. */
void igvk_frame_viewport(int x, int y, int w, int h, float minz, float maxz);

/*
 * How many frames were presented, and how many were skipped for want of a
 * swapchain texture.
 *
 * Printed at shutdown. A renderer that presents zero frames and a renderer
 * that was never called look identical on a black screen, and this is what
 * tells them apart.
 */
void igvk_device_report(void);

#endif /* IGVK_DEVICE_H */
