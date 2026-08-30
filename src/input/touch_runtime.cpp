#include "touch_runtime.h"

#include "touch_controls.h"
#include "../native/dinput_pad_virtual.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace {

struct ContactState {
    float x = 0.0F;
    float y = 0.0F;
    bool active = false;
};

x2::input::TouchControls controls;
std::map<SDL_FingerID, ContactState> contacts;
SDL_Window *window;

const char *button_name(x2::input::TouchAction action)
{
    using x2::input::TouchAction;
    switch (action) {
    case TouchAction::LowAttack: return "a";
    case TouchAction::HighAttack: return "b";
    case TouchAction::Jump: return "y";
    case TouchAction::Guard: return "x";
    case TouchAction::Power: return "righttrigger";
    case TouchAction::Ally: return "lefttrigger";
    case TouchAction::TargetLock: return "rightshoulder";
    case TouchAction::NextHero: return "up";
    case TouchAction::PreviousHero: return "down";
    case TouchAction::DecreaseAggr: return "left";
    case TouchAction::IncreaseAggr: return "right";
    case TouchAction::MapToggle: return "rightstick";
    case TouchAction::Pause: return "start";
    case TouchAction::Stats: return "back";
    default: return nullptr;
    }
}

const char *axis_name(x2::input::TouchAction action)
{
    using x2::input::TouchAction;
    switch (action) {
    case TouchAction::Forward:
    case TouchAction::Backward: return "lefty";
    case TouchAction::MoveLeft:
    case TouchAction::MoveRight: return "leftx";
    case TouchAction::CameraUp:
    case TouchAction::CameraDown: return "righty";
    case TouchAction::CameraLeft:
    case TouchAction::CameraRight: return "rightx";
    default: return nullptr;
    }
}

void publish(const x2::input::ActionEvent &event)
{
    const bool release = event.phase == lucent::touch::Phase::ended ||
                         event.phase == lucent::touch::Phase::canceled;
    const char *button = button_name(event.action);
    const char *axis = axis_name(event.action);
    char reason[256];
    if (button) {
        if (release) {
            if (!dinput_pad_virtual_release(button))
                std::fprintf(stderr, "touch: could not release virtual button %s\n", button);
        } else if (!dinput_pad_virtual_set(button, event.value, 0.12, reason,
                                           sizeof reason)) {
            std::fprintf(stderr, "touch: could not press virtual button %s: %s\n",
                         button, reason);
        }
        return;
    }
    if (axis) {
        if (release) {
            if (!dinput_pad_virtual_release(axis))
                std::fprintf(stderr, "touch: could not release virtual axis %s\n", axis);
        } else if (!dinput_pad_virtual_set(axis, event.value, 0.18, reason,
                                           sizeof reason)) {
            std::fprintf(stderr, "touch: could not move virtual axis %s: %s\n",
                         axis, reason);
        }
    }
}

void publish(const std::vector<x2::input::ActionEvent> &events)
{
    for (const auto &event : events) publish(event);
}

} // namespace

void x2_touch_runtime_window(SDL_Window *new_window)
{
    publish(controls.cancel());
    contacts.clear();
    window = new_window;
    if (!window) return;
    int width = 0;
    int height = 0;
    if (!SDL_GetWindowSizeInPixels(window, &width, &height) || width <= 0 || height <= 0)
        return;
    SDL_Rect safe{0, 0, width, height};
    if (!SDL_GetWindowSafeArea(window, &safe)) safe = {0, 0, width, height};
    controls.set_viewport({static_cast<float>(width), static_cast<float>(height),
                           {static_cast<float>(safe.x), static_cast<float>(safe.y),
                            static_cast<float>(width - safe.x - safe.w),
                            static_cast<float>(height - safe.y - safe.h)}});
}

int x2_touch_runtime_event(const SDL_Event *event)
{
    if (!window || !event) return 0;
    if (event->type != SDL_EVENT_FINGER_DOWN &&
        event->type != SDL_EVENT_FINGER_MOTION &&
        event->type != SDL_EVENT_FINGER_UP &&
        event->type != SDL_EVENT_FINGER_CANCELED)
        return 0;
    int width = 0;
    int height = 0;
    if (!SDL_GetWindowSizeInPixels(window, &width, &height)) return 1;
    const auto &finger = event->tfinger;
    auto &contact = contacts[finger.fingerID];
    contact.x = finger.x * static_cast<float>(width);
    contact.y = finger.y * static_cast<float>(height);
    contact.active = event->type != SDL_EVENT_FINGER_UP &&
                     event->type != SDL_EVENT_FINGER_CANCELED;
    std::vector<lucent::touch::Contact> active;
    active.reserve(contacts.size());
    const auto phase = event->type == SDL_EVENT_FINGER_DOWN
                           ? lucent::touch::Phase::began
                           : event->type == SDL_EVENT_FINGER_MOTION
                                 ? lucent::touch::Phase::moved
                                 : event->type == SDL_EVENT_FINGER_UP
                                       ? lucent::touch::Phase::ended
                                       : lucent::touch::Phase::canceled;
    for (const auto &[id, value] : contacts)
        if (value.active || id == finger.fingerID)
            active.push_back({static_cast<std::int64_t>(id), {value.x, value.y},
                              id == finger.fingerID ? phase
                                                    : lucent::touch::Phase::moved});
    publish(controls.route(active));
    if (!contact.active) contacts.erase(finger.fingerID);
    return 1;
}

void x2_touch_runtime_cancel(void)
{
    publish(controls.cancel());
    contacts.clear();
}
