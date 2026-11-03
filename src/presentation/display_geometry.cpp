#include "display_geometry.h"

#ifdef X2_WITH_SDL
#include <SDL3/SDL.h>
#include <lucent/cvar_c.h>
#include <stdio.h>
#include <string.h>
#endif

namespace x2::presentation {

#ifdef X2_WITH_SDL
namespace {

bool emulated_display(unsigned *width, unsigned *height) {
  const char *text = lucent_cvar_text("display.emulate");
  unsigned w = 0, h = 0;
  char tail = 0;

  if (!text || !*text || sscanf(text, "%ux%u%c", &w, &h, &tail) != 2 || !w ||
      !h)
    return false;
  if (width && height) {
    *width = w;
    *height = h;
  }
  return true;
}

} // namespace
#endif

int display_pixel_size(unsigned *width, unsigned *height) {
#ifdef X2_WITH_SDL
  if (width && height && emulated_display(width, height))
    return 1;
  /* The seed asks before anything else has started video in a --no-window
     run; the answer must be the display, not "unknown". */
  if (width && height &&
      (SDL_WasInit(SDL_INIT_VIDEO) || SDL_InitSubSystem(SDL_INIT_VIDEO))) {
    SDL_DisplayID display = SDL_GetPrimaryDisplay();
    const SDL_DisplayMode *mode =
        display ? SDL_GetDesktopDisplayMode(display) : NULL;
    if (mode && mode->w > 0 && mode->h > 0) {
      float density = mode->pixel_density > 0.0f ? mode->pixel_density : 1.0f;
      *width = (unsigned)((float)mode->w * density + 0.5f);
      *height = (unsigned)((float)mode->h * density + 0.5f);
      return 1;
    }
  }
#else
  (void)width;
  (void)height;
#endif
  return 0;
}

bool host_window_fills_display() {
#ifdef X2_WITH_SDL
  const char *driver = SDL_GetCurrentVideoDriver();

  if (emulated_display(nullptr, nullptr))
    return true;
  return driver &&
         (strcmp(driver, "android") == 0 || strcmp(driver, "uikit") == 0);
#else
  return false;
#endif
}

int window_pixel_size(SDL_Window *window, unsigned *width, unsigned *height) {
#ifdef X2_WITH_SDL
  int w = 0, h = 0;

  if (window && width && height && SDL_GetWindowSizeInPixels(window, &w, &h) &&
      w > 0 && h > 0) {
    *width = (unsigned)w;
    *height = (unsigned)h;
    return 1;
  }
#else
  (void)window;
  (void)width;
  (void)height;
#endif
  return 0;
}

} // namespace x2::presentation
