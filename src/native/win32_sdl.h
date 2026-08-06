/*
 * What the rest of the host may know about the Win32/SDL layer.
 *
 * win32_sdl.c backs the guest's single HWND with one SDL_Window. The renderer
 * needs that window to put a swapchain on, and reaching into the file's static
 * would be the alternative -- so this is the one thing it exports.
 *
 * SDL_Window is named as an incomplete type rather than by including SDL3, so
 * a caller that only needs to pass the pointer along does not acquire SDL's
 * headers, and the pointer still cannot be confused with any other.
 */
#ifndef WIN32_SDL_H
#define WIN32_SDL_H

struct SDL_Window;

/* The guest's window, or NULL if the guest has not created one (or the run
   is --no-window). NULL is a normal state, not an error: the headless
   harness runs this way on purpose. */
struct SDL_Window *win32_sdl_window(void);

#endif /* WIN32_SDL_H */
