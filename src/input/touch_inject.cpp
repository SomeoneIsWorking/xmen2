/* Driving a contact from outside the host's own touchscreen. */
#include "touch_inject.h"

#include "revive_prompt.hpp"
#include "touch_runtime.h"

#include <SDL3/SDL.h>

namespace x2::input {

int touch_inject(int64_t contact_id, float x, float y, TouchPhase phase) {
  SDL_Event event{};
  switch (phase) {
  case TouchPhase::Down:
    event.type = SDL_EVENT_FINGER_DOWN;
    break;
  case TouchPhase::Motion:
    event.type = SDL_EVENT_FINGER_MOTION;
    break;
  case TouchPhase::Up:
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
  /* The host pump's order: the revive prompt before the touch controls. */
  if (x2::input::revive_prompt_event(event)) {
    return 1;
  }
  return x2_touch_runtime_event(&event);
}

} // namespace x2::input
