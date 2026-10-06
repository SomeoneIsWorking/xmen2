#include "window_settings.h"

#include <SDL3/SDL.h>
#include <stdio.h>

static int g_owns_geometry;

static int fail(char *why, int whyn, const char *operation) {
  if (why && whyn > 0)
    snprintf(why, (size_t)whyn, "%s failed: %s", operation, SDL_GetError());
  return 0;
}

static int set_exclusive_mode(SDL_Window *window, unsigned width,
                              unsigned height) {
  SDL_DisplayMode **modes;
  const SDL_DisplayMode *best = NULL;
  SDL_DisplayID display = SDL_GetDisplayForWindow(window);
  int count = 0, i;

  modes = SDL_GetFullscreenDisplayModes(display, &count);
  int applied;
  if (!modes)
    return 0;
  for (i = 0; i < count; i++)
    if ((unsigned)modes[i]->w == width && (unsigned)modes[i]->h == height &&
        (!best || modes[i]->refresh_rate > best->refresh_rate))
      best = modes[i];
  applied = best && SDL_SetWindowFullscreenMode(window, best);
  SDL_free(modes);
  return applied;
}

int x2_window_settings_apply(SDL_Window *window, const X2Settings *settings,
                             char *why, int whyn) {
  if (!window || !settings) {
    if (why && whyn > 0)
      snprintf(why, (size_t)whyn, "window and settings are required");
    return 0;
  }
  if (settings->window_mode == X2_WINDOW_WINDOWED) {
    if (!SDL_SetWindowFullscreen(window, false))
      return fail(why, whyn, "leaving fullscreen");
    if (!SDL_SetWindowFullscreenMode(window, NULL))
      return fail(why, whyn, "clearing the exclusive display mode");
    if (!SDL_SetWindowBordered(window, true) ||
        !SDL_SetWindowResizable(window, true))
      return fail(why, whyn, "restoring the window frame");
    if (!SDL_SetWindowSize(window, (int)settings->width, (int)settings->height))
      return fail(why, whyn, "setting the windowed resolution");
    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED,
                          SDL_WINDOWPOS_CENTERED);
  } else if (settings->window_mode == X2_WINDOW_BORDERLESS) {
    if (!SDL_SetWindowFullscreenMode(window, NULL))
      return fail(why, whyn, "selecting desktop fullscreen");
    if (!SDL_SetWindowBordered(window, false) ||
        !SDL_SetWindowResizable(window, false) ||
        !SDL_SetWindowFullscreen(window, true))
      return fail(why, whyn, "entering borderless fullscreen");
  } else if (settings->window_mode == X2_WINDOW_FULLSCREEN) {
    if (!set_exclusive_mode(window, settings->width, settings->height)) {
      if (why && whyn > 0)
        snprintf(why, (size_t)whyn, "display has no %ux%u exclusive mode",
                 settings->width, settings->height);
      return 0;
    }
    if (!SDL_SetWindowBordered(window, false) ||
        !SDL_SetWindowResizable(window, false) ||
        !SDL_SetWindowFullscreen(window, true))
      return fail(why, whyn, "entering exclusive fullscreen");
  } else {
    if (why && whyn > 0)
      snprintf(why, (size_t)whyn, "unknown window mode %d",
               (int)settings->window_mode);
    return 0;
  }
  if (!SDL_SyncWindow(window))
    return fail(why, whyn, "synchronising window");
  g_owns_geometry = 1;
  if (why && whyn > 0)
    snprintf(why, (size_t)whyn, "%s %ux%u",
             x2_window_mode_name(settings->window_mode), settings->width,
             settings->height);
  return 1;
}

int x2_window_settings_owns_geometry(void) { return g_owns_geometry; }
