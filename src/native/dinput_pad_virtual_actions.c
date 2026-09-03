/* Input actions for the synthetic pad are separate from its lifecycle and
 * device-discovery code. Touch controls and the HTTP test channel use this
 * owner to publish and release virtual state. */
#include "dinput_pad_virtual.h"
#include "dinput_pad_virtual_internal.h"

#include "dinput_pad.h"
#include "guest_clock.h"

#include <stdio.h>
#include <string.h>

int dinput_pad_virtual_release(const char *what) {
#ifdef X2_WITH_SDL
  int i;
  if (!g_virt_js || !what)
    return 0;
  for (i = 0; i < X2_VIRTUAL_BUTTON_COUNT; i++) {
    if (!strcmp(what, g_vbtn_name[i])) {
      if (!SDL_SetJoystickVirtualButton(g_virt_js, i, false))
        return 0;
      g_vbtn_until[i] = 0.0;
      SDL_UpdateJoysticks();
      SDL_UpdateGamepads();
      return 1;
    }
  }
  if (!strcmp(what, "up") || !strcmp(what, "down") || !strcmp(what, "left") ||
      !strcmp(what, "right")) {
    if (!SDL_SetJoystickVirtualHat(g_virt_js, 0, SDL_HAT_CENTERED))
      return 0;
    SDL_UpdateJoysticks();
    SDL_UpdateGamepads();
    return 1;
  }
  for (i = 0; i < X2_VIRTUAL_AXIS_COUNT; i++) {
    if (!strcmp(what, g_vaxis_name[i])) {
      const short rest = axis_is_trigger(i) ? trigger_raw(0.0) : 0;
      if (!SDL_SetJoystickVirtualAxis(g_virt_js, i, rest))
        return 0;
      g_vaxis_value[i] = rest;
      g_vaxis_until[i] = 0.0;
      SDL_UpdateJoysticks();
      SDL_UpdateGamepads();
      return 1;
    }
  }
  return 0;
#else
  (void)what;
  return 0;
#endif
}

/* Release whatever has been held long enough. Called once a frame beside the
 * attach/detach schedule, so a press lasts real frames rather than one poll. */
void virtual_expire(void) {
#ifdef X2_WITH_SDL
  double now = guest_clock_now_s();
  int i;
  if (!g_virt_js)
    return;
  for (i = 0; i < X2_VIRTUAL_BUTTON_COUNT; i++)
    if (g_vbtn_until[i] != 0.0 && now >= g_vbtn_until[i]) {
      double held = now - g_vbtn_until[i];
      g_vbtn_until[i] = 0.0;
      g_vbtn_clears++;
      if (g_vbtn_clears <= 4)
        fprintf(stderr,
                "DINPUT-PAD: releasing button %d, %.3fs past "
                "its deadline (clear #%lu)\n",
                i, held, g_vbtn_clears);
      SDL_SetJoystickVirtualButton(g_virt_js, i, false);
    }
  for (i = 0; i < X2_VIRTUAL_AXIS_COUNT; i++)
    if (g_vaxis_until[i] != 0.0 && now >= g_vaxis_until[i]) {
      short rest = axis_is_trigger(i) ? trigger_raw(0.0) : 0;
      g_vaxis_until[i] = 0.0;
      g_vaxis_value[i] = rest;
      SDL_SetJoystickVirtualAxis(g_virt_js, i, rest);
    }
#endif
}
