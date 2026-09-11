#ifndef X2_DISPLAY_GEOMETRY_H
#define X2_DISPLAY_GEOMETRY_H

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
 * Window rects are NOT this measurement. src/native/win32_sdl.c answers
 * GetWindowRect/GetClientRect for the desktop HWND in window coordinates --
 * points -- because the rects it returns for the game's own window come from
 * SDL_GetWindowSize in the same space, and a desktop rect in pixels beside a
 * window rect in points is a comparison the guest would get wrong.
 */
int x2_display_pixel_size(unsigned *width, unsigned *height);

#endif /* X2_DISPLAY_GEOMETRY_H */
