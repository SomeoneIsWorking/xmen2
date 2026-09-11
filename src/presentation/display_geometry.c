#include "display_geometry.h"

#ifdef X2_WITH_SDL
#include <SDL3/SDL.h>
#endif

int x2_display_pixel_size(unsigned *width, unsigned *height) {
#ifdef X2_WITH_SDL
  if (width && height && SDL_WasInit(SDL_INIT_VIDEO)) {
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
