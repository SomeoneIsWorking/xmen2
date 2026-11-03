#pragma once

struct SDL_Window;

namespace x2::presentation {

/*
 * The primary display's size in PIXELS, or 0 when SDL cannot say.
 *
 * One owner, because the conversion is not obvious and getting it wrong is
 * expensive: SDL reports a HiDPI desktop in scaled points -- a 4K screen says
 * 1536x864 at pixel_density 2.5 -- and a 2005 D3D8 title has no notion of
 * display scaling, so the only number that means anything to it is the pixel
 * count. Handing the guest the logical size made its own legality pass refuse
 * every mode above it: 1920x1080 on a 3840x2160 monitor came back as "too big
 * for the desktop" and the engine silently built an 800x600 device instead.
 *
 * What to DO when the answer is unavailable is deliberately not decided here.
 * GetDeviceCaps reports whether the value was measured and otherwise assumes
 * 1024x768, D3D8's legality pass keeps its 1280x1024 constant, and the port's
 * resolution ladder falls back to 16:9. Those are three policies over one
 * measurement.
 *
 * Window rects are NOT this measurement. src/native/win32_sdl.cpp answers
 * GetWindowRect/GetClientRect for the desktop HWND in window coordinates --
 * points -- because the rects it returns for the game's own window come from
 * SDL_GetWindowSize in the same space, and a desktop rect in pixels beside a
 * window rect in points is a comparison the guest would get wrong.
 *
 * Video is started here when nothing has started it yet, so the answer is
 * the same in a --no-window run as in a shown one.
 *
 * `display.emulate=WxH` replaces the measurement and makes the host window
 * fill the display, so a desktop run presents as a phone does.
 */
int display_pixel_size(unsigned *width, unsigned *height);

/* Whether the host gives the game one window that always fills the display:
   Android's and iOS's video drivers, or display.emulate. Window geometry is
   then the host's, not a setting. */
bool host_window_fills_display();

/* The output surface's size in pixels, from the live window. */
int window_pixel_size(SDL_Window *window, unsigned *width, unsigned *height);

} // namespace x2::presentation
