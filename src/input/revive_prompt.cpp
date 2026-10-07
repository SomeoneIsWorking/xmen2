#include "revive_prompt.hpp"

#include <SDL3/SDL.h>

namespace x2::input {
namespace {

RevivePrompt prompt;

bool inside(float x, float y) {
  return x >= kRevivePromptRect.left && x <= kRevivePromptRect.right &&
         y >= kRevivePromptRect.top && y <= kRevivePromptRect.bottom;
}

} // namespace

void RevivePrompt::offer(const std::string &text, bool affordable) {
  const std::lock_guard lock(mutex_);
  offered_ = true;
  affordable_ = affordable;
  text_ = text;
}

void RevivePrompt::withdraw() {
  const std::lock_guard lock(mutex_);
  offered_ = false;
  requested_ = false;
  text_.clear();
}

void RevivePrompt::announce(const std::string &text) {
  const std::lock_guard lock(mutex_);
  notice_ = text;
  notice_until_ =
      std::chrono::steady_clock::now() +
      std::chrono::duration_cast<std::chrono::steady_clock::duration>(
          std::chrono::duration<double>(notice_seconds_));
}

bool RevivePrompt::request() {
  const std::lock_guard lock(mutex_);
  if (!offered_) {
    return false;
  }
  requested_ = true;
  return true;
}

bool RevivePrompt::take_request() {
  const std::lock_guard lock(mutex_);
  const bool taken = requested_;
  requested_ = false;
  return taken;
}

RevivePromptView RevivePrompt::view() const {
  const std::lock_guard lock(mutex_);
  RevivePromptView out;
  out.offered = offered_;
  out.affordable = affordable_;
  out.text = text_;
  if (std::chrono::steady_clock::now() < notice_until_) {
    out.notice = notice_;
  }
  return out;
}

bool RevivePrompt::handle_event(const SDL_Event &event) {
  switch (event.type) {
  case SDL_EVENT_KEY_DOWN:
    if (event.key.key == SDLK_F3 && !event.key.repeat) {
      return request();
    }
    return false;
  case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    if (event.gbutton.button == SDL_GAMEPAD_BUTTON_LEFT_SHOULDER) {
      return request();
    }
    return false;
  case SDL_EVENT_FINGER_DOWN: {
    if (!inside(event.tfinger.x, event.tfinger.y)) {
      return false;
    }
    if (!request()) {
      return false;
    }
    const std::lock_guard lock(mutex_);
    finger_ = static_cast<long long>(event.tfinger.fingerID);
    return true;
  }
  case SDL_EVENT_FINGER_MOTION:
  case SDL_EVENT_FINGER_UP:
  case SDL_EVENT_FINGER_CANCELED: {
    const std::lock_guard lock(mutex_);
    if (finger_ != static_cast<long long>(event.tfinger.fingerID)) {
      return false;
    }
    if (event.type != SDL_EVENT_FINGER_MOTION) {
      finger_ = -1;
    }
    return true;
  }
  default:
    return false;
  }
}

void RevivePrompt::reset() {
  const std::lock_guard lock(mutex_);
  offered_ = false;
  affordable_ = false;
  requested_ = false;
  text_.clear();
  notice_.clear();
  notice_until_ = {};
  finger_ = -1;
}

RevivePrompt &revive_prompt() { return prompt; }

bool revive_prompt_event(const SDL_Event &event) {
  return revive_prompt().handle_event(event);
}

} // namespace x2::input
