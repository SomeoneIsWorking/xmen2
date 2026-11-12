#include "binding_capture.hpp"

#include <SDL3/SDL.h>

#include <mutex>

#include "dinput_system.h"
#include "settings_store.h"
#include "x2_log.h"

namespace x2::input {
namespace {

class BindingCapture {
public:
  void begin(CaptureTarget target) {
    const std::lock_guard<std::mutex> hold(lock_);
    target_ = target;
    releasing_ = Release::None;
  }

  std::optional<CaptureTarget> target() {
    const std::lock_guard<std::mutex> hold(lock_);
    return target_;
  }

  bool active() {
    const std::lock_guard<std::mutex> hold(lock_);
    return target_.has_value() || releasing_ != Release::None;
  }

  bool event(const SDL_Event &event) {
    const std::lock_guard<std::mutex> hold(lock_);
    if (releasing_ != Release::None) {
      if (released(event)) {
        releasing_ = Release::None;
      }
      return input(event);
    }
    if (!target_) {
      return false;
    }
    if (event.type == SDL_EVENT_KEY_DOWN && event.key.repeat == 0) {
      key(event);
    } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
      cancel(Release::Mouse, event.button.button);
    } else if (event.type == SDL_EVENT_FINGER_DOWN) {
      cancel(Release::Finger, 0u);
    } else if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
      cancel(Release::Pad, event.gbutton.button);
    }
    return input(event);
  }

private:
  enum class Release { None, Key, Mouse, Finger, Pad };

  static bool input(const SDL_Event &event) {
    switch (event.type) {
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
    case SDL_EVENT_TEXT_INPUT:
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
    case SDL_EVENT_MOUSE_WHEEL:
    case SDL_EVENT_FINGER_DOWN:
    case SDL_EVENT_FINGER_UP:
    case SDL_EVENT_FINGER_MOTION:
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
      return true;
    default:
      return false;
    }
  }

  bool released(const SDL_Event &event) const {
    switch (releasing_) {
    case Release::Key:
      return event.type == SDL_EVENT_KEY_UP && event.key.scancode == code_;
    case Release::Mouse:
      return event.type == SDL_EVENT_MOUSE_BUTTON_UP &&
             event.button.button == code_;
    case Release::Finger:
      return event.type == SDL_EVENT_FINGER_UP;
    case Release::Pad:
      return event.type == SDL_EVENT_GAMEPAD_BUTTON_UP &&
             event.gbutton.button == code_;
    case Release::None:
      return true;
    }
    return true;
  }

  void key(const SDL_Event &event) {
    const CaptureTarget target = *target_;
    target_.reset();
    releasing_ = Release::Key;
    code_ = static_cast<unsigned>(event.key.scancode);
    if (event.key.key == SDLK_ESCAPE) {
      x2_log_info("BINDING CAPTURE: cancelled by Escape.\n");
      return;
    }
    const unsigned dik = event.key.key == SDLK_DELETE
                             ? 0u
                             : dinput_system_dik(event.key.scancode);
    if (event.key.key != SDLK_DELETE && dik == 0u) {
      x2_log_info("BINDING CAPTURE: scancode %u has no DirectInput key; "
                  "the binding is unchanged.\n",
                  code_);
      return;
    }
    x2::config::Settings *settings = x2::config::settings_store();
    x2::config::KeyboardProfile &profile =
        settings->keyboard_profile[target.profile];
    const x2::config::KeyboardProfile before = profile;
    profile.keyboard[target.row] = static_cast<std::uint16_t>(dik);
    profile.keyboard_set[target.row] = 1u;
    char why[256] = "";
    if (!x2::config::settings_store_save(why, static_cast<int>(sizeof why))) {
      profile = before;
      x2_log_error("BINDING CAPTURE: keyboard %u row %u was not saved: %s.\n",
                   target.profile + 1u, target.row, why);
      return;
    }
    x2_log_info("BINDING CAPTURE: keyboard %u row %u is now DIK 0x%02x.\n",
                target.profile + 1u, target.row, dik);
  }

  void cancel(Release release, unsigned code) {
    target_.reset();
    releasing_ = release;
    code_ = code;
    x2_log_info("BINDING CAPTURE: cancelled.\n");
  }

  std::mutex lock_;
  std::optional<CaptureTarget> target_;
  Release releasing_ = Release::None;
  unsigned code_ = 0u;
};

BindingCapture g_capture;

} // namespace

void binding_capture_begin(CaptureTarget target) { g_capture.begin(target); }

std::optional<CaptureTarget> binding_capture_target() {
  return g_capture.target();
}

bool binding_capture_active() { return g_capture.active(); }

bool binding_capture_event(const SDL_Event &event) {
  return g_capture.event(event);
}

} // namespace x2::input
