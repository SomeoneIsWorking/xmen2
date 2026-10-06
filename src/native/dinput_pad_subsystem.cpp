#include "dinput_pad.h"
/*
 * Starting SDL's gamepad subsystem under this layer's input policy. Its own
 * file because three owners start it -- the inventory's rescan, the synthetic
 * pad, and the browser host's early start (sdl_host_setup.h) -- and the
 * policy set before the start must be the same for all of them.
 */
#ifdef X2_WITH_SDL
#include <SDL3/SDL.h>
#endif

int dinput_pad_subsystem_start(void) {
#ifdef X2_WITH_SDL
  if (SDL_WasInit(SDL_INIT_GAMEPAD))
    return 1;
  /*
   * SDL DROPS JOYSTICK BUTTONS WHEN NOTHING HAS KEYBOARD FOCUS, and it
   * drops them in a way that is almost impossible to see: axis state is
   * still written through, so the pad enumerates, its axes move, and
   * every button reads released forever. Measured here as 71,700 button
   * polls with 0 down while a press was held across thousands of them.
   *
   * That is the wrong policy for THIS program whatever the run looks
   * like. The window belongs to the guest -- a 2005 game creating it
   * through a Win32 layer that SDL only backs -- so SDL's notion of
   * which window holds focus is not something the port controls, and a
   * headless run has a hidden window that can never take focus at all.
   * Gating input on it means input silently disappearing.
   *
   * Set BEFORE the subsystem starts: the hint is read at init.
   */
  SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
  return SDL_InitSubSystem(SDL_INIT_GAMEPAD) ? 1 : 0;
#else
  return 0;
#endif
}
