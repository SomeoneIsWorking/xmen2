#include "touch_runtime.h"
#include "../native/x2_log.h"

extern "C" {
#include "../native/guest_clock.h"
}
#include "gameplay_control.h"

#include "../config/settings.h"
#include "../config/settings_store.h"
#include "../native/dinput_pad.h"
#include "../native/dinput_pad_report.h"
#include "../native/dinput_pad_virtual.h"
#include "touch_census.h"
#include "touch_controls.h"
#include "touch_pad.h"
#include "touch_source.h"
#include "transient_controller_assignment.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <deque>
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
x2::input::PortraitPointer portrait_pointer;
std::deque<X2TouchPointer> pending_pointers;
SDL_Window *window;

/* The run's counts belong to touch_census, which owns the text made from
   them; this only adds to them at the points where each outcome is decided. */
X2TouchCensus &census = *x2_touch_census();
/* The one viewport both the control zones and the HUD relocation lay out
   from. Set with the window, so neither owner computes its own. */
X2LayoutViewport viewport;

const char *button_name(x2::input::TouchAction action) {
  using x2::input::TouchAction;
  switch (action) {
  case TouchAction::LightAttack:
    return "a";
  case TouchAction::HeavyAttack:
    return "b";
  case TouchAction::Jump:
    return "y";
  case TouchAction::Use:
    return "x";
  case TouchAction::Powers:
    return "righttrigger";
  case TouchAction::EnergyPack:
    return "lefttrigger";
  case TouchAction::HealthPack:
    return "rightshoulder";
  case TouchAction::NextHero:
    return "up";
  case TouchAction::PreviousHero:
    return "down";
  case TouchAction::DecreaseAggr:
    return "left";
  case TouchAction::IncreaseAggr:
    return "right";
  case TouchAction::MapToggle:
    return "rightstick";
  case TouchAction::Pause:
    return "start";
  case TouchAction::Stats:
    return "back";
  default:
    return nullptr;
  }
}

/* Set at the first published press: the value of the game's button-read
   counter at that moment, plus one so that zero still means "no press yet". */
unsigned long first_press_reads = 0;
bool told_first_release = false;

void publish_button(const x2::input::ActionEvent &event) {
  const bool withdrawn = event.phase == lucent::touch::Phase::canceled;
  const bool release = event.phase == lucent::touch::Phase::ended || withdrawn;
  const char *button = button_name(event.action);
  char reason[256];
  if (!button)
    return;
  if (release) {
    /* A cancelled press is taken back, not completed, so it does not wait for
       the game to read it. */
    if (withdrawn ? dinput_pad_virtual_release_now(button)
                  : dinput_pad_virtual_release(button)) {
      census.buttons_published++;
      /*
       * How many times did the game ASK while that press was held?
       *
       * "Published and never seen" has two causes that look identical in a
       * total: the state was not visible to the reader, or the reader never
       * ran while it was set. Only a count taken across the press itself
       * tells them apart.
       */
      if (first_press_reads && !told_first_release) {
        X2PadPollCounts counts;
        told_first_release = true;
        dinput_pad_poll_counts(&counts);
        x2_log_error("touch: first press of \"%s\" released -- the game read "
                     "a button %lu time(s) while it was held, %lu of them "
                     "DOWN\n",
                     button, counts.button_reads - (first_press_reads - 1),
                     counts.buttons_down);
      }
    } else {
      census.buttons_refused++;
      x2_log_error("touch: could not release virtual button %s\n", button);
    }
  } else if (dinput_pad_virtual_set(button, event.value, -1.0, reason,
                                    sizeof reason)) {
    census.buttons_published++;
    /*
     * The FIRST press says what reading it back found, once.
     *
     * "Published" only means the set was accepted. Whether the game can see
     * it is a different layer, and the pad's own read-back already knows --
     * it was being computed and thrown away here. A browser run published
     * every press and the game read a button 109,780 times with none ever
     * down; this is the line that would have said which layer lost it.
     */
    if (!first_press_reads) {
      X2PadPollCounts counts;
      dinput_pad_poll_counts(&counts);
      first_press_reads = counts.button_reads + 1;
      x2_log_error("touch: first press of \"%s\" published -- %s\n", button,
                   reason);
    }
  } else {
    census.buttons_refused++;
    x2_log_error("touch: could not press virtual button %s: %s\n", button,
                 reason);
  }
}

void publish_axis(std::span<const x2::input::ActionEvent> events,
                  const char *name, x2::input::TouchAction negative,
                  x2::input::TouchAction positive) {
  const auto value = x2::input::touch_axis_value(events, negative, positive);
  if (!value)
    return;
  const bool released = std::any_of(
      events.begin(), events.end(), [negative, positive](const auto &event) {
        return (event.action == negative || event.action == positive) &&
               (event.phase == lucent::touch::Phase::ended ||
                event.phase == lucent::touch::Phase::canceled);
      });
  char reason[256];
  if (released) {
    if (dinput_pad_virtual_release(name)) {
      census.axes_published++;
    } else {
      census.axes_refused++;
      x2_log_error("touch: could not release virtual axis %s\n", name);
    }
  } else if (dinput_pad_virtual_set(name, *value, 0.0, reason, sizeof reason)) {
    census.axes_published++;
  } else {
    census.axes_refused++;
    x2_log_error("touch: could not move virtual axis %s: %s\n", name, reason);
  }
}

void publish(const std::vector<x2::input::ActionEvent> &events) {
  using x2::input::TouchAction;
  if (events.empty())
    return;
  x2::input::touch_pad::ensure();
  x2::input::touch_pad::claim_player_one();
  for (const auto &event : portrait_pointer.route(events)) {
    const bool release = event.phase == lucent::touch::Phase::ended ||
                         event.phase == lucent::touch::Phase::canceled;
    pending_pointers.push_back({1, event.position.x, event.position.y,
                                release ? 0
                                : event.phase == lucent::touch::Phase::began
                                    ? 1
                                    : -1,
                                static_cast<uint32_t>(SDL_GetTicks())});
  }
  for (const auto &event : events) {
    if (event.phase == lucent::touch::Phase::ended ||
        event.phase == lucent::touch::Phase::canceled)
      active_zones.erase(event.zone_id);
    else
      active_zones.insert(event.zone_id);
  }
  for (const auto &event : events)
    publish_button(event);
  publish_axis(events, "lefty", TouchAction::Forward, TouchAction::Backward);
  publish_axis(events, "leftx", TouchAction::MoveLeft, TouchAction::MoveRight);
  publish_axis(events, "righty", TouchAction::CameraUp,
               TouchAction::CameraDown);
  publish_axis(events, "rightx", TouchAction::CameraLeft,
               TouchAction::CameraRight);
}

} // namespace

void x2_touch_runtime_window(SDL_Window *new_window) {
  publish(controls.cancel());
  contacts.clear();
  publish(controls.set_portraits({}, 0));
  window = new_window;
  if (!window)
    return;
  x2::input::touch_pad::prepare_for_host();
  int width = 0;
  int height = 0;
  if (!SDL_GetWindowSizeInPixels(window, &width, &height) || width <= 0 ||
      height <= 0)
    return;
  SDL_Rect safe{0, 0, width, height};
  if (!SDL_GetWindowSafeArea(window, &safe))
    safe = {0, 0, width, height};
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

int x2_touch_runtime_viewport(X2LayoutViewport *out) {
  if (!out || !window)
    return 0;
  *out = viewport;
  return 1;
}

int x2_touch_runtime_event(const SDL_Event *event) {
  if (!event)
    return 0;
  const bool is_finger = event->type == SDL_EVENT_FINGER_DOWN ||
                         event->type == SDL_EVENT_FINGER_MOTION ||
                         event->type == SDL_EVENT_FINGER_UP ||
                         event->type == SDL_EVENT_FINGER_CANCELED;
  /* Counted before the gates, because "no contact ever arrived" and "contacts
     arrived and were dropped" are the two answers the report has to tell
     apart, and only this side of the gates can see the second one. */
  if (is_finger) {
    switch (event->type) {
    case SDL_EVENT_FINGER_DOWN:
      census.contacts_down++;
      break;
    case SDL_EVENT_FINGER_MOTION:
      census.contacts_moved++;
      break;
    case SDL_EVENT_FINGER_UP:
      census.contacts_up++;
      break;
    default:
      census.contacts_canceled++;
      break;
    }
  }
  if (!window) {
    if (is_finger)
      census.ignored_no_window++;
    return 0;
  }
  if (!x2_touch_runtime_overlay_visible()) {
    if (is_finger)
      census.ignored_overlay_hidden++;
    if (!contacts.empty())
      x2_touch_runtime_cancel();
    return 0;
  }
  if (!is_finger)
    return 0;
  int width = 0;
  int height = 0;
  if (!SDL_GetWindowSizeInPixels(window, &width, &height))
    return 1;
  const auto &finger = event->tfinger;
  auto &contact = contacts[finger.fingerID];
  contact.x = finger.x * static_cast<float>(width);
  contact.y = finger.y * static_cast<float>(height);
  contact.active = event->type != SDL_EVENT_FINGER_UP &&
                   event->type != SDL_EVENT_FINGER_CANCELED;
  std::vector<lucent::touch::Contact> active;
  active.reserve(contacts.size());
  const auto phase =
      event->type == SDL_EVENT_FINGER_DOWN     ? lucent::touch::Phase::began
      : event->type == SDL_EVENT_FINGER_MOTION ? lucent::touch::Phase::moved
      : event->type == SDL_EVENT_FINGER_UP     ? lucent::touch::Phase::ended
                                               : lucent::touch::Phase::canceled;
  for (const auto &[id, value] : contacts)
    if (value.active || id == finger.fingerID)
      active.push_back(
          {static_cast<std::int64_t>(id),
           {value.x, value.y},
           id == finger.fingerID ? phase : lucent::touch::Phase::moved});
  const auto actions = controls.route(active);
  census.zone_presses += actions.size();
  publish(actions);
  if (!contact.active)
    contacts.erase(finger.fingerID);
  return 1;
}

void x2_touch_runtime_lifecycle_event(const SDL_Event *event) {
  if (!window || !event)
    return;
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

void x2_touch_runtime_cancel(void) {
  census.cancellations++;
  publish(controls.cancel());
  publish(controls.set_portraits({}, 0));
  contacts.clear();
  active_zones.clear();
}

void x2_touch_runtime_hud_regions(const X2Rect portraits[4],
                                  unsigned visible_mask) {
  if (!portraits || !x2_touch_runtime_overlay_visible())
    publish(controls.set_portraits({}, 0));
  else
    publish(controls.set_portraits(std::span{portraits, 4}, visible_mask));
}

int x2_touch_runtime_take_pointer(X2TouchPointer *pointer) {
  if (!x2_touch_runtime_overlay_visible() && !contacts.empty())
    x2_touch_runtime_cancel();
  if (!pointer || pending_pointers.empty())
    return 0;
  *pointer = pending_pointers.front();
  pending_pointers.pop_front();
  return 1;
}

size_t x2_touch_runtime_visuals(X2TouchVisual *out, size_t capacity) {
  const auto zones = controls.zones();
  const size_t visible_count = static_cast<size_t>(
      std::count_if(zones.begin(), zones.end(),
                    [](const auto &zone) { return zone.visible; }));
  if (!out)
    return visible_count;
  size_t output_index = 0;
  for (const auto &visual : zones) {
    if (!visual.visible)
      continue;
    if (output_index < capacity)
      out[output_index] = {
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

void x2_touch_runtime_note_source(const SDL_Event *event) {
  const bool was_touch = x2_touch_source_is_touch() != 0;
  x2_touch_source_note(event);
  if (was_touch && !x2_touch_source_is_touch()) {
    /* Whatever was under a finger is not held any more: the zones that were
       down would otherwise stay down with the overlay gone. */
    x2_touch_runtime_cancel();
  }
}

int x2_touch_runtime_active(void) {
  /* The setting can force either end on every platform. ALWAYS is what makes
     the layout reachable on a desktop with no touchscreen -- a layout nobody
     can see until it is on a phone is a layout that gets shipped wrong. */
  const unsigned mode = x2_settings_store()->touch_controls;
  return mode == X2_TOUCH_CONTROLS_ALWAYS ||
         (mode == X2_TOUCH_CONTROLS_AUTO && x2_touch_source_is_touch());
}

int x2_touch_runtime_overlay_visible(void) {
  return window != nullptr && x2_touch_runtime_active() &&
         x2_gameplay_control_active(guest_clock_now_s());
}

void x2_touch_runtime_report(const char *tag) {
  x2_touch_census_report(tag, window != nullptr,
                         x2::input::touch_pad::host_devices(),
                         x2::input::touch_pad::host_capable());
}
