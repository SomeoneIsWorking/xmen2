#include "touch_pad_publisher.h"

#include "../native/dinput_pad.h"
#include "../native/dinput_pad_report.h"
#include "../native/dinput_pad_virtual.h"
#include "../native/x2_log.h"
#include "touch_census.h"
#include "touch_pad.h"

#include <algorithm>

namespace x2::input {
namespace {

/* The one place an action becomes a pad button. Derived from the Xbox release
   bindings in src/native/xbox_defaults.c, never invented per screen. */
const char *button_name(TouchAction action) {
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

bool is_release(lucent::touch::Phase phase) {
  return phase == lucent::touch::Phase::ended ||
         phase == lucent::touch::Phase::canceled;
}

} // namespace

void PadPublisher::publish_button(const ActionEvent &event) {
  const bool withdrawn = event.phase == lucent::touch::Phase::canceled;
  const char *button = button_name(event.action);
  X2TouchCensus &census = *x2_touch_census();
  char reason[256];
  if (!button) {
    return;
  }
  if (is_release(event.phase)) {
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
      if (first_press_reads_ && !told_first_release_) {
        X2PadPollCounts counts;
        told_first_release_ = true;
        dinput_pad_poll_counts(&counts);
        x2_log_error("touch: first press of \"%s\" released -- the game read "
                     "a button %lu time(s) while it was held, %lu of them "
                     "DOWN\n",
                     button, counts.button_reads - (first_press_reads_ - 1),
                     counts.buttons_down);
      }
    } else {
      census.buttons_refused++;
      x2_log_error("touch: could not release virtual button %s\n", button);
    }
    return;
  }
  if (dinput_pad_virtual_set(button, event.value, -1.0, reason,
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
    if (!first_press_reads_) {
      X2PadPollCounts counts;
      dinput_pad_poll_counts(&counts);
      first_press_reads_ = counts.button_reads + 1;
      x2_log_error("touch: first press of \"%s\" published -- %s\n", button,
                   reason);
    }
    return;
  }
  census.buttons_refused++;
  x2_log_error("touch: could not press virtual button %s: %s\n", button,
               reason);
}

void PadPublisher::publish_axis(std::span<const ActionEvent> events,
                                const char *name, TouchAction negative,
                                TouchAction positive) {
  const auto value = touch_axis_value(events, negative, positive);
  X2TouchCensus &census = *x2_touch_census();
  char reason[256];
  if (!value) {
    return;
  }
  const bool released = std::any_of(
      events.begin(), events.end(),
      [negative, positive](const ActionEvent &event) {
        return (event.action == negative || event.action == positive) &&
               is_release(event.phase);
      });
  if (released) {
    if (dinput_pad_virtual_release(name)) {
      census.axes_published++;
    } else {
      census.axes_refused++;
      x2_log_error("touch: could not release virtual axis %s\n", name);
    }
    return;
  }
  if (dinput_pad_virtual_set(name, *value, 0.0, reason, sizeof reason)) {
    census.axes_published++;
    return;
  }
  census.axes_refused++;
  x2_log_error("touch: could not move virtual axis %s: %s\n", name, reason);
}

void PadPublisher::publish(std::span<const ActionEvent> events) {
  if (events.empty()) {
    return;
  }
  /* The pad exists before the guest enumerates controllers, and a pad no
     player resolves to is one the guest never polls -- measured in a browser
     as 48 contacts published to a pad read zero times. */
  touch_pad::ensure();
  touch_pad::claim_player_one();
  for (const auto &event : events) {
    publish_button(event);
  }
  publish_axis(events, "lefty", TouchAction::Forward, TouchAction::Backward);
  publish_axis(events, "leftx", TouchAction::MoveLeft, TouchAction::MoveRight);
  publish_axis(events, "righty", TouchAction::CameraUp,
               TouchAction::CameraDown);
  publish_axis(events, "rightx", TouchAction::CameraLeft,
               TouchAction::CameraRight);
}

} // namespace x2::input
