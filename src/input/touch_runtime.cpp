#include "touch_runtime.h"

extern "C" {
#include "../native/guest_clock.h"
}
#include "gameplay_control.h"

#include "touch_controls.h"
#include "../native/dinput_pad_virtual.h"
#include "../config/settings.h"
#include "../config/settings_store.h"
#include "transient_controller_assignment.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <map>
#include <set>
#include <span>
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
std::set<std::uint32_t> active_zones;
SDL_Window *window;
/* The one viewport both the control zones and the HUD relocation lay out
   from. Set with the window, so neither owner computes its own. */
X2LayoutViewport viewport;

const char *button_name(x2::input::TouchAction action)
{
    using x2::input::TouchAction;
    switch (action) {
    case TouchAction::LightAttack: return "a";
    case TouchAction::HeavyAttack: return "b";
    case TouchAction::Jump: return "y";
    case TouchAction::Use: return "x";
    case TouchAction::Powers: return "righttrigger";
    case TouchAction::EnergyPack: return "lefttrigger";
    case TouchAction::HealthPack: return "rightshoulder";
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

bool is_portrait_action(x2::input::TouchAction action)
{
    using x2::input::TouchAction;
    return action == TouchAction::SelectHero1 ||
           action == TouchAction::SelectHero2 ||
           action == TouchAction::SelectHero3 ||
           action == TouchAction::SelectHero4;
}

void publish_button(const x2::input::ActionEvent &event)
{
    const bool release = event.phase == lucent::touch::Phase::ended ||
                         event.phase == lucent::touch::Phase::canceled;
    const char *button = button_name(event.action);
    char reason[256];
    if (!button) return;
    if (release) {
        if (!dinput_pad_virtual_release(button))
            std::fprintf(stderr, "touch: could not release virtual button %s\n", button);
    } else if (!dinput_pad_virtual_set(button, event.value, -1.0, reason,
                                       sizeof reason)) {
        std::fprintf(stderr, "touch: could not press virtual button %s: %s\n",
                     button, reason);
    }
}

void publish_axis(std::span<const x2::input::ActionEvent> events,
                  const char *name, x2::input::TouchAction negative,
                  x2::input::TouchAction positive)
{
    const auto value = x2::input::touch_axis_value(events, negative, positive);
    if (!value) return;
    const bool released = std::any_of(events.begin(), events.end(),
                                     [negative, positive](const auto &event) {
                                         return (event.action == negative ||
                                                 event.action == positive) &&
                                                (event.phase == lucent::touch::Phase::ended ||
                                                 event.phase == lucent::touch::Phase::canceled);
                                     });
    char reason[256];
    if (released) {
        if (!dinput_pad_virtual_release(name))
            std::fprintf(stderr, "touch: could not release virtual axis %s\n", name);
    } else if (!dinput_pad_virtual_set(name, *value, 0.0, reason, sizeof reason)) {
        std::fprintf(stderr, "touch: could not move virtual axis %s: %s\n", name,
                     reason);
    }
}

/*
 * Claim player one for the pad that touch publishes through.
 *
 * A pad only reaches the guest once a player resolves to it, and a player
 * resolves only from an explicit transient assignment or a persisted
 * reservation. On a phone neither exists on a first run, so every touch was
 * routed into a pad no player was reading: the probe reported the game
 * polling buttons that were never down, while SDL's own touch-to-mouse
 * emulation carried presses to menus and nothing to gameplay.
 *
 * A controller the player already chose keeps player one, so plugging a real
 * pad in still wins; this only fills the vacancy.
 */
void claim_player_one()
{
    static bool attempted = false;
    if (attempted) return;
    if (x2_transient_controller_has_assignment(0) ||
        x2_settings_player_controller(x2_settings_store(), 0))
        return;
    const int slot = dinput_pad_virtual_slot();
    if (slot < 0) return;  /* Not opened yet; try again on the next contact. */
    attempted = true;
    if (!x2_transient_controller_assign(slot, 0))
        std::fprintf(stderr, "touch: could not assign the touch pad (slot %d) "
                             "to player 1; touch will not reach gameplay\n",
                     slot);
}

void publish(const std::vector<x2::input::ActionEvent> &events)
{
    using x2::input::TouchAction;
    claim_player_one();
    for (const auto &event : events) {
        if (event.phase == lucent::touch::Phase::ended ||
            event.phase == lucent::touch::Phase::canceled)
            active_zones.erase(event.zone_id);
        else
            active_zones.insert(event.zone_id);
    }
    for (const auto &event : events) publish_button(event);
    publish_axis(events, "lefty", TouchAction::Forward, TouchAction::Backward);
    publish_axis(events, "leftx", TouchAction::MoveLeft, TouchAction::MoveRight);
    publish_axis(events, "righty", TouchAction::CameraUp, TouchAction::CameraDown);
    publish_axis(events, "rightx", TouchAction::CameraLeft, TouchAction::CameraRight);
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
    viewport = {static_cast<float>(width),
                static_cast<float>(height),
                static_cast<float>(safe.x),
                static_cast<float>(safe.y),
                static_cast<float>(width - safe.x - safe.w),
                static_cast<float>(height - safe.y - safe.h)};
    /* One viewport, two consumers: the controls and the relocated HUD read
       the same numbers, which is what stops the drawn HUD and the touchable
       zones from disagreeing about where the screen is. */
    controls.set_viewport({viewport.width,
                           viewport.height,
                           {viewport.safe_left, viewport.safe_top,
                            viewport.safe_right, viewport.safe_bottom}});
}

int x2_touch_runtime_viewport(X2LayoutViewport *out)
{
    if (!out || !window) return 0;
    *out = viewport;
    return 1;
}

int x2_touch_runtime_event(const SDL_Event *event, X2TouchPointer *pointer)
{
    if (pointer) *pointer = {};
    if (!window || !event) return 0;
    if (!x2_settings_store()->touch_controls) {
        if (!contacts.empty()) x2_touch_runtime_cancel();
        return 0;
    }
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
    const auto actions = controls.route(active);
    publish(actions);
    if (pointer) {
        const auto selected = std::find_if(
            actions.begin(), actions.end(), [&finger](const auto &action) {
                return action.contact_id == static_cast<std::int64_t>(finger.fingerID) &&
                       is_portrait_action(action.action);
            });
        if (selected != actions.end()) {
            pointer->valid = 1;
            pointer->x = selected->position.x;
            pointer->y = selected->position.y;
            pointer->button_change = selected->phase == lucent::touch::Phase::began
                                         ? 1
                                         : selected->phase == lucent::touch::Phase::ended ||
                                                   selected->phase == lucent::touch::Phase::canceled
                                               ? 0
                                               : -1;
            pointer->time_ms = static_cast<uint32_t>(finger.timestamp / 1000000u);
        }
    }
    if (!contact.active) contacts.erase(finger.fingerID);
    return 1;
}

void x2_touch_runtime_lifecycle_event(const SDL_Event *event)
{
    if (!window || !event) return;
    if (event->type == SDL_EVENT_WINDOW_FOCUS_LOST ||
        event->type == SDL_EVENT_WINDOW_HIDDEN ||
        event->type == SDL_EVENT_WINDOW_MINIMIZED) {
        x2_touch_runtime_cancel();
    } else if (event->type == SDL_EVENT_WINDOW_RESIZED ||
               event->type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED ||
               event->type == SDL_EVENT_WINDOW_SAFE_AREA_CHANGED) {
        x2_touch_runtime_window(window);
    }
}

void x2_touch_runtime_cancel(void)
{
    publish(controls.cancel());
    contacts.clear();
    active_zones.clear();
}

size_t x2_touch_runtime_visuals(X2TouchVisual *out, size_t capacity)
{
    const auto zones = controls.zones();
    const size_t visible_count = static_cast<size_t>(std::count_if(
        zones.begin(), zones.end(), [](const auto &zone) { return zone.visible; }));
    if (!out) return visible_count;
    size_t output_index = 0;
    for (const auto &visual : zones) {
        if (!visual.visible) continue;
        if (output_index < capacity) out[output_index] = {
            visual.zone.id,
            visual.zone.left,
            visual.zone.top,
            visual.zone.right,
            visual.zone.bottom,
            static_cast<int>(visual.action),
            active_zones.contains(visual.zone.id) ? 1 : 0,
            visual.stick ? 1 : 0,
        };
        ++output_index;
    }
    return visible_count;
}

int x2_touch_runtime_overlay_visible(void)
{
    /* The setting decides, on every platform. It used to be compiled out
       except on Android, which made the touch layout unreachable on the one
       host where it can be iterated on and looked at -- and a layout nobody
       can see until it is on a phone is a layout that gets shipped wrong.
       Desktop defaults are the player's to set; see settings.c. */
    return window != nullptr && x2_settings_store()->touch_controls != 0 &&
           x2_gameplay_control_active(guest_clock_now_s());
}
