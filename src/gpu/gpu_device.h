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
#ifndef GPU_DEVICE_H
#define GPU_DEVICE_H

#include <stdint.h>

/*
 * Create the GPU device. Returns 1 on success, 0 on failure.
 *
 * Failure is loud and immediate rather than deferred: a renderer with no
 * device produces a blank window, and a blank window is the single hardest
 * symptom to attribute. Called from the engine's own userInstantiate, so the
 * failure is reported at the moment the engine asked for a renderer.
 */
int gpu_device_create(void);
void gpu_device_destroy(void);

/* 1 once a device exists. This is what igDxVisualContext::getLastError's
   `this+0x144 == 0 -> error` test becomes for this backend. */
int gpu_device_ready(void);

/* The D3D device's logical backbuffer size. Colour and automatic depth are
   replaced as one transaction; failure preserves the active pair. The
   window/swapchain is an independent presentation target and may have any
   dimensions. Headless output is a harness-owned target, not a Port Settings
   path, and keeps its explicit diagnostic dimensions. */
int gpu_device_set_backbuffer_size(uint32_t width, uint32_t height);

/*
 * The window the swapchain presents to.
 *
 * The engine hands us a Win32 HWND through setNativeWindowHandle, which is
 * meaningless here; the host owns its own window and this is how the two are
 * connected. Passing NULL detaches. Returns 1 if the swapchain was claimed.
 */
struct SDL_Window;
int gpu_device_attach_window(struct SDL_Window *w);

/*
 * Render with NO window at all.
 *
 * A hidden window is not a substitute: SDL hands back no swapchain image for
 * one, so every frame is refused and a "headless" run draws nothing while
 * still counting frames. Measured, on Wayland -- 5400 draws refused per five
 * seconds with "no frame is open".
 *
 * In headless mode the frame goes into an off-screen colour target of the
 * given size instead. Everything downstream is unchanged, because the pass
 * already targets whatever `g_swap` points at, and it is what makes a
 * screenshot possible with no X server in the picture.
 */
void gpu_device_headless(int on, uint32_t w, uint32_t h);

/* The headless target's pixels, BGRA8, w*h*4 bytes. 0 (and says why) when the
   run is not headless or no frame has been rendered yet. */
int  gpu_device_headless_read(void *bgra_out, uint32_t bytes,
                              uint32_t *w_out, uint32_t *h_out);

/* The size a readback would need, or 0 with the reason in `why` -- not
   headless, or no frame rendered yet. Those are different answers and a caller
   deciding what to report has to be able to tell them apart. */
int  gpu_device_headless_size(uint32_t *w_out, uint32_t *h_out, char *why,
                              int whyn);

/*
 * Where to find a window if none is attached yet.
 *
 * The guest creates its window long after the renderer is instantiated, so the
 * frame path re-tries the attach -- but it must not know WHERE the window
 * comes from. x2native installs a provider that returns the guest's; a native
 * front-end attaches its own window and installs nothing. Without this, this
 * file included the guest's Win32 layer, which contradicts the whole point of
 * the file comment above.
 */
void gpu_device_set_window_provider(struct SDL_Window *(*fn)(void));

/*
 * The frame.
 *
 * igDxVisualContext's frame boundary is BeginScene / EndScene+Present, driven
 * by slots 174 and 175. These are the same boundary: begin acquires the
 * swapchain texture and opens a command buffer, end submits it.
 *
 * gpu_frame_begin returns 0 when there is nothing to draw into (no device,
 * no window, or the swapchain had no texture ready this frame). A 0 must be
 * propagated -- the engine's beginDraw returns a bool for exactly this and
 * skips the frame when it is false.
 */
int  gpu_frame_begin(void);
void gpu_frame_end(void);
int  gpu_frame_in_progress(void);

/*
 * Clear the current render destination.
 *
 * `mask` is the engine's own flag byte from clearRenderDestination: bit 0
 * colour, bit 1 depth, bit 2 stencil. Colour components are 0..1.
 *
 * SDL_GPU clears as part of beginning a render pass rather than as a command,
 * so this records what the next pass must clear with. A clear requested AFTER
 * the pass has begun -- which the engine does, 3,833 times in one gameplay
 * run -- ends that pass and starts another with this clear as its load op.
 * The attachments not named in `mask` are LOADed, so nothing already drawn is
 * lost; that is what makes the reopen equivalent to the clear D3D8 asked for
 * rather than a frame restarted.
 */
void gpu_frame_clear(unsigned mask, float r, float g, float b, float a,
                      float depth, uint32_t stencil);

/*
 * Frames actually presented. The game's own measure of progress, which is what
 * makes it a better schedule for a scripted run than the wall clock -- see
 * X2_INPUT_SCRIPT's `f` form in dinput_device.c.
 */
unsigned long gpu_frames_presented(void);

/*
 * The frame-phase profiler's device side, for the heartbeat.
 *
 * Frame WALL time is present-to-present and so includes the guest's share;
 * the counterpart read gpu_draw_perf() supplies the host's draw/upload share.
 * The histogram buckets are milliseconds; `hist` must be valid across the
 * read and the bucket layout is fixed by gpu_frame_end's table, not by this
 * caller's count.
 */
void gpu_device_perf(unsigned long long *frame_ns,
                     unsigned long long *frame_ns_min,
                     unsigned long long *frame_ns_max,
                     unsigned long long *end_submits,
                     unsigned long *intervals,
                     const unsigned long **hist);
/* True when the frame currently ending received at least one programmable
   draw, even if X2_DRAW_RANGE skipped it. Used by content-selected captures. */
int gpu_frame_had_programmable(void);

/*
 * 1 once X2_MAX_FRAMES frames have been presented.
 *
 * This game never stops on its own, so every measured run has been ended by a
 * timeout: each costs its whole timeout even when the thing being measured
 * finished a minute earlier, and the reports come out of a signal handler.
 * This is a clean stop on the game's OWN counter. It is a FLAG rather than an
 * exit because stopping is not this file's business -- whoever runs the
 * reports polls it.
 */
int gpu_frame_limit_reached(void);

/*
 * Which render destination the engine has bound.
 *
 * This backend has exactly one: the swapchain. The engine's render
 * destinations are a pool it indexes, and index 0 is the back buffer -- so
 * any OTHER index is an off-screen target that does not exist here, and
 * drawing would silently go to the wrong place. That case reports itself
 * rather than being ignored.
 */
void gpu_frame_bind_target(unsigned index);

/* Viewport in pixels, already clamped by the engine to the render
   destination. minz/maxz are the depth range. */
void gpu_frame_viewport(int x, int y, int w, int h, float minz, float maxz);

/*
 * How many frames were presented, and how many were skipped for want of a
 * swapchain texture.
 *
 * Printed at shutdown. A renderer that presents zero frames and a renderer
 * that was never called look identical on a black screen, and this is what
 * tells them apart.
 */
void gpu_device_report(void);

#endif /* GPU_DEVICE_H */
