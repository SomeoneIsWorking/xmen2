/* Driving a contact from outside the host's own touchscreen. */
#include "touch_inject.h"

#include "touch_runtime.h"

#include <SDL3/SDL.h>

int x2_touch_inject(int64_t contact_id, float x, float y, X2TouchPhase phase) {
  SDL_Event event{};
  switch (phase) {
  case X2_TOUCH_PHASE_DOWN:
    event.type = SDL_EVENT_FINGER_DOWN;
    break;
  case X2_TOUCH_PHASE_MOTION:
    event.type = SDL_EVENT_FINGER_MOTION;
    break;
  case X2_TOUCH_PHASE_UP:
    event.type = SDL_EVENT_FINGER_UP;
    break;
  default:
    event.type = SDL_EVENT_FINGER_CANCELED;
    break;
  }
  event.tfinger.fingerID = static_cast<SDL_FingerID>(contact_id);
  event.tfinger.x = x;
  event.tfinger.y = y;
  /* The source verdict is part of what a contact does, and the real pump
     notes it before routing. An injected contact that skipped this would
     leave AUTO reporting "not touch" while touch was being driven. */
  x2_touch_runtime_note_source(&event);
  return x2_touch_runtime_event(&event);
}
