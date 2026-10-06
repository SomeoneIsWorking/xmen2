#include "touch_pointer.h"

#include <SDL3/SDL.h>

namespace x2::input {
namespace {

/* What a phase does to the one button: press, release, or move the cursor
   without touching it. */
int button_change_for(lucent::touch::Phase phase) {
  switch (phase) {
  case lucent::touch::Phase::began:
    return 1;
  case lucent::touch::Phase::ended:
  case lucent::touch::Phase::canceled:
    return 0;
  default:
    return -1;
  }
}

} // namespace

void RetailPointer::queue(lucent::touch::Point at, int button_change) {
  pending_.push_back(
      {1, at.x, at.y, button_change, static_cast<uint32_t>(SDL_GetTicks())});
}

bool RetailPointer::contact(std::int64_t contact_id, lucent::touch::Point at,
                            lucent::touch::Phase phase) {
  if (!owner_.accepts(contact_id, phase)) {
    return false;
  }
  at_ = at;
  queue(at, button_change_for(phase));
  return true;
}

void RetailPointer::resolved(lucent::touch::Point at,
                             lucent::touch::Phase phase) {
  queue(at, button_change_for(phase));
}

bool RetailPointer::release_if_held() {
  if (!owner_.held()) {
    return false;
  }
  owner_.release();
  queue(at_, 0);
  return true;
}

bool RetailPointer::take(X2TouchPointer &out) {
  if (pending_.empty()) {
    return false;
  }
  out = pending_.front();
  pending_.pop_front();
  return true;
}

} // namespace x2::input
