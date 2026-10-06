#include "touch_source.h"

#include <SDL3/SDL.h>

/* Half of SDL's signed axis range: past this the player moved the stick and
   is not merely resting a thumb on it, or letting a pad in a drawer drift. */
#define PAD_AXIS_INTENT 16384

static int g_touch;

/* Did this port make that event itself?

   The on-screen pad publishes through an SDL virtual joystick, and SDL
   announces every button and axis it sets as an ordinary joystick and gamepad
   event. Read as a controller arriving, those events put the overlay away and
   cancelled the very press that produced them -- in a browser the press was
   taken back a millisecond after it was made, and the heartbeat said "source
   says not touch" while contacts were still arriving. It is the same
   confusion the SDL_TOUCH_MOUSEID checks below already avoid: that pad is the
   same finger. */
static unsigned int g_own_pad;

void x2_touch_source_set_own_pad(unsigned int joystick_id) {
  g_own_pad = joystick_id;
}

static int is_our_own_pad(SDL_JoystickID which) {
  return g_own_pad != 0u && (unsigned int)which == g_own_pad;
}

void x2_touch_source_note(const union SDL_Event *event) {
  if (!event)
    return;
  switch (event->type) {
  case SDL_EVENT_FINGER_DOWN:
  case SDL_EVENT_FINGER_MOTION:
  case SDL_EVENT_FINGER_UP:
  case SDL_EVENT_FINGER_CANCELED:
    g_touch = 1;
    return;
  case SDL_EVENT_MOUSE_MOTION:
    /* SDL reports touch as mouse motion as well unless the host turns that
       off. That synthetic pointer is the same finger, so reading it as a
       mouse would put the on-screen pad away the instant it was used. */
    if (event->motion.which == SDL_TOUCH_MOUSEID)
      return;
    break;
  case SDL_EVENT_MOUSE_BUTTON_DOWN:
    if (event->button.which == SDL_TOUCH_MOUSEID)
      return;
    break;
  case SDL_EVENT_GAMEPAD_AXIS_MOTION:
    if (is_our_own_pad(event->gaxis.which))
      return;
    if (event->gaxis.value > -PAD_AXIS_INTENT &&
        event->gaxis.value < PAD_AXIS_INTENT)
      return;
    break;
  case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    if (is_our_own_pad(event->gbutton.which))
      return;
    break;
  case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
    if (is_our_own_pad(event->jbutton.which))
      return;
    break;
  case SDL_EVENT_JOYSTICK_HAT_MOTION:
    if (is_our_own_pad(event->jhat.which))
      return;
    break;
  case SDL_EVENT_JOYSTICK_AXIS_MOTION:
    if (is_our_own_pad(event->jaxis.which))
      return;
    if (event->jaxis.value > -PAD_AXIS_INTENT &&
        event->jaxis.value < PAD_AXIS_INTENT)
      return;
    break;
  case SDL_EVENT_KEY_DOWN:
    break;
  default:
    return;
  }
  g_touch = 0;
}

int x2_touch_source_is_touch(void) { return g_touch; }

void x2_touch_source_reset(void) {
  g_touch = 0;
  g_own_pad = 0;
}
