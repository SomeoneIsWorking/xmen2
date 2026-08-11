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

/*
 * --no-window: the guest's window is created HIDDEN.
 *
 * The flag used to skip only x2native's own probe window, so a "headless" run
 * still opened the game's window on whatever desktop SDL found -- which is
 * fine for playing and wrong for a test, because a test that takes over the
 * screen cannot be run while its author is using the machine. The window still
 * EXISTS (the swapchain needs one, and so do the input paths); it is not shown.
 */
void win32_sdl_hide_windows(int hide);

/*
 * A modal dialog with buttons, on SDL -- the replacement for USER32's dialog
 * family, which this port does not implement (see the note in win32_sdl.c).
 * `ids` are the values the guest's own dialog would have returned, and one of
 * them comes back. `fallback` is answered, and reported as this host's choice
 * rather than a user's, when there is no screen to show a modal on.
 *
 * The text is written to stderr in every case, before the box is shown.
 */
int win32_sdl_dialog(const char *title, const char *text,
                     const char *const *labels, const int *ids, int n,
                     int fallback);

#endif /* WIN32_SDL_H */
