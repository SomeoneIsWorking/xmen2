/* The gate that decides whether an on-screen thumbstick is drawn over the
   retail HUD. It is tested on both answers and on the two events that must
   NOT change it: a touchscreen reported as a synthetic mouse, and a resting
   controller stick. A gate that only ever says "yes" would look identical to
   the platform test this replaced. */
#include "../src/input/touch_source.h"

#include <SDL3/SDL.h>

#include <stdio.h>

static unsigned checks, failures;

static void check(int ok, const char *what) {
  checks++;
  if (!ok) {
    failures++;
    printf("FAIL: %s\n", what);
  }
}

static void note(SDL_Event event) { x2_touch_source_note(&event); }

static SDL_Event finger(Uint32 type) {
  SDL_Event event;
  SDL_zero(event);
  event.type = type;
  return event;
}

int main(void) {
  SDL_Event event;

  x2_touch_source_reset();
  check(!x2_touch_source_is_touch(), "no input yet is not touch");

  note(finger(SDL_EVENT_FINGER_DOWN));
  check(x2_touch_source_is_touch(), "a contact is touch");
  note(finger(SDL_EVENT_FINGER_MOTION));
  note(finger(SDL_EVENT_FINGER_UP));
  check(x2_touch_source_is_touch(), "lifting the finger stays touch");

  /* Touch reported a second time as a mouse is still the same finger. */
  SDL_zero(event);
  event.type = SDL_EVENT_MOUSE_MOTION;
  event.motion.which = SDL_TOUCH_MOUSEID;
  x2_touch_source_note(&event);
  check(x2_touch_source_is_touch(), "synthetic touch-mouse motion is not a mouse");
  SDL_zero(event);
  event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
  event.button.which = SDL_TOUCH_MOUSEID;
  x2_touch_source_note(&event);
  check(x2_touch_source_is_touch(), "synthetic touch-mouse button is not a mouse");

  /* A stick at rest, and one barely off centre, are not the player. */
  SDL_zero(event);
  event.type = SDL_EVENT_GAMEPAD_AXIS_MOTION;
  event.gaxis.value = 0;
  x2_touch_source_note(&event);
  event.gaxis.value = -4000;
  x2_touch_source_note(&event);
  check(x2_touch_source_is_touch(), "a resting pad stick does not take over");

  /* Events from devices nobody is holding say nothing either way. */
  SDL_zero(event);
  event.type = SDL_EVENT_GAMEPAD_ADDED;
  x2_touch_source_note(&event);
  check(x2_touch_source_is_touch(), "plugging a pad in is not using it");

  x2_touch_source_note(NULL);
  check(x2_touch_source_is_touch(), "no event changes nothing");

  SDL_zero(event);
  event.type = SDL_EVENT_GAMEPAD_AXIS_MOTION;
  event.gaxis.value = -20000;
  x2_touch_source_note(&event);
  check(!x2_touch_source_is_touch(), "a deflected stick is the player");

  note(finger(SDL_EVENT_FINGER_DOWN));
  check(x2_touch_source_is_touch(), "touching again is touch again");
  SDL_zero(event);
  event.type = SDL_EVENT_KEY_DOWN;
  x2_touch_source_note(&event);
  check(!x2_touch_source_is_touch(), "a key press puts the pad away");

  note(finger(SDL_EVENT_FINGER_DOWN));
  SDL_zero(event);
  event.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
  x2_touch_source_note(&event);
  check(!x2_touch_source_is_touch(), "a pad button puts the pad away");

  note(finger(SDL_EVENT_FINGER_DOWN));
  SDL_zero(event);
  event.type = SDL_EVENT_MOUSE_MOTION;
  event.motion.which = 1;
  x2_touch_source_note(&event);
  check(!x2_touch_source_is_touch(), "a real mouse puts the pad away");

  x2_touch_source_reset();
  check(!x2_touch_source_is_touch(), "reset clears the history");

  printf("%u check(s), %u failure(s)\n", checks, failures);
  return failures ? 1 : 0;
}
