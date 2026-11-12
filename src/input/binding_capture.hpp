#ifndef X2_BINDING_CAPTURE_HPP
#define X2_BINDING_CAPTURE_HPP

#include <optional>

union SDL_Event;

/* Rebinding one action of a keyboard profile: the next key pressed becomes
   its binding, Delete unbinds it, and Escape, a click, a tap or a pad
   button cancels. From begin until that key or button is released every
   host input event is taken here, and the game reads no input, so neither
   the key nor the release reaches the menu that started the capture. */
namespace x2::input {

struct CaptureTarget {
  unsigned profile;
  unsigned row;
};

void binding_capture_begin(CaptureTarget target);
/* The row waiting for its key, if any. */
std::optional<CaptureTarget> binding_capture_target();
/* True while waiting for a key or for the release of the one that ended it. */
bool binding_capture_active();
/* Host event pump: true when `event` belongs to the capture. */
bool binding_capture_event(const SDL_Event &event);

} // namespace x2::input

#endif
