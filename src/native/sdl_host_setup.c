#include "sdl_host_setup.h"

#include "dinput_pad.h"
#include "x2_log.h"

#ifdef X2_WITH_SDL
#include <SDL3/SDL.h>

#include <lucent/log_c.h>

static LucentLogLevel level_for(SDL_LogPriority priority) {
  switch (priority) {
  case SDL_LOG_PRIORITY_ERROR:
  case SDL_LOG_PRIORITY_CRITICAL:
    return LUCENT_LOG_ERROR;
  case SDL_LOG_PRIORITY_WARN:
    return LUCENT_LOG_WARN;
  case SDL_LOG_PRIORITY_INFO:
    return LUCENT_LOG_INFO;
  default:
    return LUCENT_LOG_DEBUG;
  }
}

static void route(void *userdata, int category, SDL_LogPriority priority,
                  const char *message) {
  (void)userdata;
  (void)category;
  lucent_log(level_for(priority), "sdl", "%s", message);
}

int sdl_host_setup(int window) {
  SDL_SetLogOutputFunction(route, NULL);
  if (!SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0")) {
    x2_log_error("x2native: could not disable SDL touch-to-mouse events: %s\n",
                 SDL_GetError());
    return 0;
  }
#ifdef __EMSCRIPTEN__
  (void)window;
  if (!SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_SENSOR) ||
      !dinput_pad_subsystem_start()) {
    x2_log_error("x2native: SDL would not start its browser event sources on "
                 "the main thread: %s\n",
                 SDL_GetError());
    return 0;
  }
#else
  if (window && !SDL_InitSubSystem(SDL_INIT_VIDEO)) {
    x2_log_error("x2native: SDL video would not start: %s\n", SDL_GetError());
    return 0;
  }
#endif
  return 1;
}
#else
int sdl_host_setup(int window) {
  (void)window;
  return 1;
}
#endif
