#include "touch_runtime.h"

extern "C" {
#include "../native/guest_clock.h"
}
#include "gameplay_control.h"

#include "../config/settings.h"
#include "../config/settings_store.h"
#include "touch_census.h"
#include "touch_controls.h"
#include "touch_menu_controls.h"
#include "touch_pad.h"
#include "touch_pad_publisher.h"
#include "touch_pointer.h"
#include "touch_skip_button.h"
#include "touch_source.h"
#include "touch_visuals.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <set>
#include <span>
#include <vector>

namespace x2::input {
namespace {

lucent::touch::Phase phase_of(Uint32 event_type) {
  switch (event_type) {
  case SDL_EVENT_FINGER_DOWN:
    return lucent::touch::Phase::began;
  case SDL_EVENT_FINGER_MOTION:
    return lucent::touch::Phase::moved;
  case SDL_EVENT_FINGER_UP:
    return lucent::touch::Phase::ended;
  default:
    return lucent::touch::Phase::canceled;
  }
}

bool is_finger(Uint32 event_type) {
  return event_type == SDL_EVENT_FINGER_DOWN ||
         event_type == SDL_EVENT_FINGER_MOTION ||
         event_type == SDL_EVENT_FINGER_UP ||
         event_type == SDL_EVENT_FINGER_CANCELED;
}

bool is_release(lucent::touch::Phase phase) {
  return phase == lucent::touch::Phase::ended ||
         phase == lucent::touch::Phase::canceled;
}

} // namespace

/*
 * WHERE A CONTACT GOES, AND NOTHING ELSE.
 *
 * SDL contact acquisition on every platform -- a desktop touchscreen, a phone
 * and a browser reach this the same way -- and the one decision that follows:
 * a finger under a drawn control is a pad press, and any other finger outside
 * gameplay is the retail GUI's pointer. Gameplay draws TouchControls; every
 * other screen draws the menu pad, MenuControls. The pad belongs to
 * PadPublisher, the Win32 pointer to RetailPointer, and the account of what
 * happened to the census. This composes them and owns only the window, the
 * live contacts, and the gate.
 */
class TouchRuntime {
public:
  void set_window(SDL_Window *window);
  bool viewport(X2LayoutViewport &out) const;

  // True when the event was this owner's to handle.
  bool handle(const SDL_Event &event);
  void handle_lifecycle(const SDL_Event &event);
  void note_source(const SDL_Event &event);

  void cancel(X2TouchCancelCause cause);
  void set_hud_regions(const X2HudRegions *regions);
  // True once per press of the port menu button.
  bool take_menu_request();
  void set_power_slots(const int icons[X2_POWER_SLOTS]);
  bool take_pointer(X2TouchPointer &out);
  std::size_t visuals(X2TouchVisual *out, std::size_t capacity) const;
  // The skip button's rectangle and whether a finger is on it, while one is
  // drawn: touch play, a window, and a skip offered.
  bool skip_button(X2Rect &rect, bool &held) const;

  bool has_window() const { return window_ != nullptr; }
  // Is there anything for the overlay document to draw -- the gameplay
  // controls, or the menu pad on a screen that is not gameplay?
  bool has_visuals() const;
  // The setting can force either end on every platform. ALWAYS is what makes
  // the layout reachable on a desktop with no touchscreen -- a layout nobody
  // can look at until it is on a phone is a layout that ships wrong.
  static bool active();
  bool overlay_visible() const;
  // The menu pad: touch play and a window, on a screen that is not gameplay
  // and is not a cinematic offering its own Skip.
  bool menu_visible() const;

private:
  struct Contact {
    float x = 0.0F;
    float y = 0.0F;
    bool active = false;
  };

  // A finger under a drawn control: zones, then the pad.
  bool route_to_controls(const SDL_Event &event);
  // A finger with no drawn control under it: the retail GUI's pointer.
  bool route_to_pointer(const SDL_Event &event);
  // A finger that began on the menu pad: its button, until it lifts.
  bool route_to_menu(const SDL_TouchFingerEvent &finger);
  // Let go of every menu pad button, when the pad stops being drawn.
  void release_menu();
  // A finger on the cinematic's skip button: the offered skip.
  bool route_to_skip(const SDL_TouchFingerEvent &finger);
  void publish(std::span<const ActionEvent> actions);
  void count_contact(Uint32 event_type) const;
  bool window_size(int &width, int &height) const;

  TouchControls controls_;
  MenuControls menu_;
  PadPublisher pad_;
  PortraitPointer portraits_;
  RetailPointer pointer_;
  SkipButton skip_;
  std::map<SDL_FingerID, Contact> contacts_;
  // Fingers the menu pad captured; every other finger outside gameplay is
  // the pointer's.
  std::map<SDL_FingerID, Contact> menu_contacts_;
  std::set<std::uint32_t> active_zones_;
  SDL_Window *window_ = nullptr;
  /* The one viewport both the control zones and the relocated HUD lay out
     from, so neither owner computes its own and they cannot disagree about
     where the screen is. */
  X2LayoutViewport viewport_{};
  bool menu_requested_ = false;
};

namespace {
/* One owner, not a drawer of loose state. The C entry points below are a shim
   over this object and hold nothing of their own. */
TouchRuntime runtime;
} // namespace

bool TouchRuntime::active() {
  const unsigned mode = x2_settings_store()->touch_controls;
  return mode == X2_TOUCH_CONTROLS_ALWAYS ||
         (mode == X2_TOUCH_CONTROLS_AUTO && x2_touch_source_is_touch());
}

bool TouchRuntime::overlay_visible() const {
  return window_ != nullptr && active() &&
         x2_gameplay_control_active(guest_clock_now_s());
}

bool TouchRuntime::menu_visible() const {
  return window_ != nullptr && active() &&
         !x2_gameplay_control_active(guest_clock_now_s()) &&
         !SkipButton::offered();
}

bool TouchRuntime::window_size(int &width, int &height) const {
  width = 0;
  height = 0;
  return SDL_GetWindowSizeInPixels(window_, &width, &height) && width > 0 &&
         height > 0;
}

void TouchRuntime::publish(std::span<const ActionEvent> actions) {
  if (actions.empty()) {
    return;
  }
  /* Selection belongs to the retail mouse handler, so a portrait tap leaves
     by the pointer and the rest by the pad. */
  for (const auto &event : portraits_.route(actions)) {
    pointer_.resolved(event.position, event.phase);
  }
  for (const auto &event : actions) {
    if (event.action == TouchAction::PortMenu &&
        event.phase == lucent::touch::Phase::began) {
      menu_requested_ = true;
    }
    if (is_release(event.phase)) {
      active_zones_.erase(event.zone_id);
    } else {
      active_zones_.insert(event.zone_id);
    }
  }
  pad_.publish(actions);
}

void TouchRuntime::set_window(SDL_Window *window) {
  publish(controls_.cancel());
  contacts_.clear();
  release_menu();
  publish(controls_.set_hud({}));
  window_ = window;
  if (!window_) {
    return;
  }
  touch_pad::prepare_for_host();
  int width = 0;
  int height = 0;
  if (!window_size(width, height)) {
    return;
  }
  SDL_Rect safe{0, 0, width, height};
  if (!SDL_GetWindowSafeArea(window_, &safe)) {
    safe = {0, 0, width, height};
  }
  viewport_ = {static_cast<float>(width),
               static_cast<float>(height),
               static_cast<float>(safe.x),
               static_cast<float>(safe.y),
               static_cast<float>(width - safe.x - safe.w),
               static_cast<float>(height - safe.y - safe.h)};
  const Viewport layout{viewport_.width,
                        viewport_.height,
                        {viewport_.safe_left, viewport_.safe_top,
                         viewport_.safe_right, viewport_.safe_bottom}};
  publish(controls_.set_viewport(layout));
  publish(menu_.set_viewport(layout));
}

bool TouchRuntime::viewport(X2LayoutViewport &out) const {
  if (!window_) {
    return false;
  }
  out = viewport_;
  return true;
}

void TouchRuntime::count_contact(Uint32 event_type) const {
  X2TouchCensus &census = *x2_touch_census();
  switch (event_type) {
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

bool TouchRuntime::route_to_menu(const SDL_TouchFingerEvent &finger) {
  int width = 0;
  int height = 0;
  if (!window_size(width, height)) {
    return false;
  }
  const lucent::touch::Phase phase = phase_of(finger.type);
  const lucent::touch::Point at{finger.x * static_cast<float>(width),
                                finger.y * static_cast<float>(height)};
  const auto held = menu_contacts_.find(finger.fingerID);
  /* A finger belongs to the pad only if it BEGAN on a button; one that began
     elsewhere is the pointer's for its whole life, so a drag across the pad
     never presses it. */
  if (held == menu_contacts_.end() &&
      (phase != lucent::touch::Phase::began || !menu_.hit(at))) {
    return false;
  }
  Contact &contact = menu_contacts_[finger.fingerID];
  contact = {at.x, at.y, !is_release(phase)};
  std::vector<lucent::touch::Contact> live;
  for (const auto &[id, value] : menu_contacts_) {
    live.push_back(
        {static_cast<std::int64_t>(id),
         {value.x, value.y},
         id == finger.fingerID ? phase : lucent::touch::Phase::moved});
  }
  const auto actions = menu_.route(live);
  x2_touch_census()->zone_presses += actions.size();
  publish(actions);
  if (!contact.active) {
    menu_contacts_.erase(finger.fingerID);
  }
  return true;
}

void TouchRuntime::release_menu() {
  publish(menu_.cancel());
  menu_contacts_.clear();
}

bool TouchRuntime::route_to_skip(const SDL_TouchFingerEvent &finger) {
  int width = 0;
  int height = 0;
  if (!window_size(width, height)) {
    return false;
  }
  return skip_.press(static_cast<std::int64_t>(finger.fingerID),
                     finger.x * static_cast<float>(width),
                     finger.y * static_cast<float>(height),
                     phase_of(finger.type), viewport_);
}

bool TouchRuntime::skip_button(X2Rect &rect, bool &held) const {
  if (!window_ || !active() || !SkipButton::offered()) {
    return false;
  }
  rect = SkipButton::place(viewport_);
  held = skip_.held();
  return true;
}

bool TouchRuntime::route_to_pointer(const SDL_Event &event) {
  X2TouchCensus &census = *x2_touch_census();
  int width = 0;
  int height = 0;
  if (!window_size(width, height)) {
    return true;
  }
  const SDL_TouchFingerEvent &finger = event.tfinger;
  const lucent::touch::Point at{finger.x * static_cast<float>(width),
                                finger.y * static_cast<float>(height)};
  if (!pointer_.contact(static_cast<std::int64_t>(finger.fingerID), at,
                        phase_of(event.type))) {
    census.pointer_refused++;
    return true;
  }
  census.pointer_events++;
  return true;
}

bool TouchRuntime::route_to_controls(const SDL_Event &event) {
  X2TouchCensus &census = *x2_touch_census();
  int width = 0;
  int height = 0;
  if (!window_size(width, height)) {
    return true;
  }
  const SDL_TouchFingerEvent &finger = event.tfinger;
  const lucent::touch::Phase phase = phase_of(event.type);
  Contact &contact = contacts_[finger.fingerID];
  contact.x = finger.x * static_cast<float>(width);
  contact.y = finger.y * static_cast<float>(height);
  contact.active = !is_release(phase);

  std::vector<lucent::touch::Contact> active;
  active.reserve(contacts_.size());
  for (const auto &[id, value] : contacts_) {
    if (value.active || id == finger.fingerID) {
      active.push_back(
          {static_cast<std::int64_t>(id),
           {value.x, value.y},
           id == finger.fingerID ? phase : lucent::touch::Phase::moved});
    }
  }
  const auto actions = controls_.route(active);
  census.zone_presses += actions.size();
  publish(actions);
  if (!contact.active) {
    contacts_.erase(finger.fingerID);
  }
  return true;
}

bool TouchRuntime::handle(const SDL_Event &event) {
  const bool finger = is_finger(event.type);
  /* Counted before the gates, because "no contact ever arrived" and "contacts
     arrived and went somewhere" are the two answers the report has to tell
     apart, and only this side of the gates can see the second one. */
  if (finger) {
    count_contact(event.type);
  }
  if (!window_) {
    if (finger) {
      x2_touch_census()->ignored_no_window++;
    }
    return false;
  }
  if (!overlay_visible()) {
    if (!contacts_.empty()) {
      cancel(X2_TOUCH_CANCEL_OVERLAY_HIDDEN);
    }
    if (!menu_visible() && !menu_contacts_.empty()) {
      release_menu();
    }
    if (!finger) {
      return false;
    }
    /* The skip button and then the menu pad first: both sit ON the retail
       GUI, and a contact that reached the pointer as well would both press
       the control and click whatever is drawn beneath it. */
    return route_to_skip(event.tfinger) ||
           (menu_visible() && route_to_menu(event.tfinger)) ||
           route_to_pointer(event);
  }
  if (!menu_contacts_.empty()) {
    release_menu();
  }
  /* Gameplay has begun under a held menu tap: retail's button must not be
     left down at a position nothing will press again. */
  if (pointer_.release_if_held()) {
    x2_touch_census()->pointer_events++;
  }
  if (!finger) {
    return false;
  }
  return route_to_controls(event);
}

void TouchRuntime::handle_lifecycle(const SDL_Event &event) {
  if (!window_) {
    return;
  }
  if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST ||
      event.type == SDL_EVENT_WINDOW_HIDDEN ||
      event.type == SDL_EVENT_WINDOW_MINIMIZED) {
    cancel(X2_TOUCH_CANCEL_WINDOW_GONE);
  } else if (event.type == SDL_EVENT_WINDOW_RESIZED ||
             event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED ||
             event.type == SDL_EVENT_WINDOW_SAFE_AREA_CHANGED) {
    set_window(window_);
  }
}

void TouchRuntime::note_source(const SDL_Event &event) {
  const bool was_touch = x2_touch_source_is_touch() != 0;
  x2_touch_source_note(&event);
  if (was_touch && !x2_touch_source_is_touch()) {
    /* Whatever was under a finger is not held any more: the zones that were
       down would otherwise stay down with the overlay gone. */
    cancel(X2_TOUCH_CANCEL_SOURCE_CHANGED);
  }
}

void TouchRuntime::cancel(X2TouchCancelCause cause) {
  X2TouchCensus &census = *x2_touch_census();
  switch (cause) {
  case X2_TOUCH_CANCEL_OVERLAY_HIDDEN:
    census.cancelled_overlay_hidden++;
    break;
  case X2_TOUCH_CANCEL_WINDOW_GONE:
    census.cancelled_window_gone++;
    break;
  case X2_TOUCH_CANCEL_SOURCE_CHANGED:
    census.cancelled_source_changed++;
    break;
  case X2_TOUCH_CANCEL_WINDOW_CHANGED:
    census.cancelled_window_changed++;
    break;
  }
  /* A lost window or a layout change lets go of the retail button too: the
     zones and the pointer are two routes out of one finger, and leaving
     either held is the same defect. The overlay merely being hidden is not
     one of them -- that is the state in which the pointer is IN USE. */
  if (cause != X2_TOUCH_CANCEL_OVERLAY_HIDDEN && pointer_.release_if_held()) {
    census.pointer_events++;
  }
  publish(controls_.cancel());
  publish(controls_.set_hud({}));
  contacts_.clear();
  release_menu();
  active_zones_.clear();
  skip_.release();
}

void TouchRuntime::set_hud_regions(const X2HudRegions *regions) {
  if (!regions || !overlay_visible()) {
    publish(controls_.set_hud({}));
  } else {
    publish(controls_.set_hud(*regions));
  }
}

bool TouchRuntime::take_menu_request() {
  const bool requested = menu_requested_;
  menu_requested_ = false;
  return requested;
}

void TouchRuntime::set_power_slots(const int icons[X2_POWER_SLOTS]) {
  std::array<int, X2_POWER_SLOTS> next{};
  std::copy(icons, icons + X2_POWER_SLOTS, next.begin());
  publish(controls_.set_power_icons(next));
}

bool TouchRuntime::take_pointer(X2TouchPointer &out) {
  if (!overlay_visible() && !contacts_.empty()) {
    cancel(X2_TOUCH_CANCEL_OVERLAY_HIDDEN);
  }
  /* Asked every frame, so a pad button held while its screen gave way to
     gameplay or a cinematic is let go even if the finger never moves. */
  if (!menu_visible() && !menu_contacts_.empty()) {
    release_menu();
  }
  return pointer_.take(out);
}

bool TouchRuntime::has_visuals() const {
  return overlay_visible() || menu_visible();
}

std::size_t TouchRuntime::visuals(X2TouchVisual *out,
                                  std::size_t capacity) const {
  const auto zones = overlay_visible() ? controls_.zones()
                     : menu_visible()
                         ? menu_.zones()
                         : std::span<const TouchControls::ZoneVisual>{};
  return overlay_visuals(zones, active_zones_, controls_.stick_deflection(),
                         controls_.stick_ring(), out, capacity);
}

} // namespace x2::input

/* ------------------------------------------------------------------------ */
/* The C surface the host event pump, the HUD and the renderer call. It holds
   no state: every entry point below forwards to the one owner above. */

using x2::input::TouchRuntime;

void x2_touch_runtime_window(SDL_Window *new_window) {
  x2::input::runtime.set_window(new_window);
}

int x2_touch_runtime_viewport(X2LayoutViewport *out) {
  return out && x2::input::runtime.viewport(*out) ? 1 : 0;
}

int x2_touch_runtime_event(const SDL_Event *event) {
  return event && x2::input::runtime.handle(*event) ? 1 : 0;
}

void x2_touch_runtime_lifecycle_event(const SDL_Event *event) {
  if (event) {
    x2::input::runtime.handle_lifecycle(*event);
  }
}

void x2_touch_runtime_note_source(const SDL_Event *event) {
  if (event) {
    x2::input::runtime.note_source(*event);
  }
}

void x2_touch_runtime_cancel(void) {
  x2::input::runtime.cancel(X2_TOUCH_CANCEL_WINDOW_CHANGED);
}

void x2_touch_runtime_cancel_because(X2TouchCancelCause cause) {
  x2::input::runtime.cancel(cause);
}

void x2_touch_runtime_hud_regions(const X2HudRegions *regions) {
  x2::input::runtime.set_hud_regions(regions);
}

int x2_touch_runtime_take_menu_request(void) {
  return x2::input::runtime.take_menu_request() ? 1 : 0;
}

void x2_touch_runtime_power_slots(const int icons[X2_POWER_SLOTS]) {
  x2::input::runtime.set_power_slots(icons);
}

int x2_touch_runtime_take_pointer(X2TouchPointer *pointer) {
  return pointer && x2::input::runtime.take_pointer(*pointer) ? 1 : 0;
}

size_t x2_touch_runtime_visuals(X2TouchVisual *out, size_t capacity) {
  return x2::input::runtime.visuals(out, capacity);
}

const char *x2_touch_runtime_action_name(int action) {
  return x2::input::touch_action_name(
      static_cast<x2::input::TouchAction>(action));
}

int x2_touch_runtime_skip_button(X2Rect *rect, int *held) {
  X2Rect placed{};
  bool pressed = false;
  if (!x2::input::runtime.skip_button(placed, pressed)) {
    return 0;
  }
  if (rect) {
    *rect = placed;
  }
  if (held) {
    *held = pressed ? 1 : 0;
  }
  return 1;
}

int x2_touch_runtime_active(void) { return TouchRuntime::active() ? 1 : 0; }

int x2_touch_runtime_overlay_visible(void) {
  return x2::input::runtime.overlay_visible() ? 1 : 0;
}

int x2_touch_runtime_has_visuals(void) {
  return x2::input::runtime.has_visuals() ? 1 : 0;
}

void x2_touch_runtime_report(const char *tag) {
  x2_touch_census_report(tag, x2::input::runtime.has_window() ? 1 : 0,
                         x2::input::touch_pad::host_devices(),
                         x2::input::touch_pad::host_capable());
}
