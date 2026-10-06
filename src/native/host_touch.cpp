#include "host_touch.h"

#include <SDL3/SDL.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

int x2_host_touch_devices(void) {
  int count = 0;
  SDL_TouchID *const devices = SDL_GetTouchDevices(&count);
  SDL_free(devices);
  return count;
}

int x2_host_touch_capable(void) {
  if (x2_host_touch_devices() > 0) {
    return 1;
  }
#ifdef __EMSCRIPTEN__
  /*
   * A browser registers no touch device until a touch has happened, so SDL's
   * list is empty on a phone that has done nothing yet -- measured: a mobile
   * viewport with touch emulation and five touch points reported 0 devices at
   * window time, and the overlay's pad was attached too late for the guest to
   * be offered it. maxTouchPoints is the capability the platform does answer
   * before the fact.
   */
  /*
   * Asked on the MAIN thread. This product runs its guest on a worker
   * (PROXY_TO_PTHREAD), and a worker's navigator is a WorkerNavigator, which
   * has no maxTouchPoints at all -- measured: the plain EM_ASM form answered
   * 0 on a mobile viewport with five emulated touch points, and the pad was
   * attached too late all over again.
   */
  return MAIN_THREAD_EM_ASM_INT(
      { return navigator.maxTouchPoints > 0 ? 1 : 0; });
#else
  return 0;
#endif
}
