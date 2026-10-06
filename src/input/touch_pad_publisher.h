#ifndef X2_TOUCH_PAD_PUBLISHER_H
#define X2_TOUCH_PAD_PUBLISHER_H

#include "touch_controls.h"

#include <cstdint>
#include <map>
#include <set>
#include <span>
#include <string>
#include <utility>

namespace x2::input {

/*
 * The far end of a touch on a drawn control: the virtual DirectInput pad.
 *
 * Touch reaches the guest as a controller and by no other route, so this owns
 * the action-to-pad-button vocabulary and every set and release made through
 * it. It also owns the diagnostic that says whether the game was READING the
 * pad while a press was held -- "published and never seen" has two causes
 * that look identical in a total, and only a count taken across the press
 * itself tells them apart. That state belongs to the publisher that takes it,
 * not to the file it used to sit in.
 */
// The pad buttons a control holds, in press order; empty for an action that
// does not reach the pad as a button (movement, camera, portraits).
std::span<const char *const> touch_action_buttons(TouchAction action);

class PadPublisher {
public:
  // Every button and axis change these actions carry, in order. Attaching the
  // pad and claiming player one for it happen here too: a pad no player is
  // reading is one the guest never polls.
  void publish(std::span<const ActionEvent> events);

private:
  void publish_button(const ActionEvent &event);
  void press(const char *button, float value);
  void release(const char *button, bool withdrawn);
  void publish_axis(std::span<const ActionEvent> events, const char *name,
                    TouchAction negative, TouchAction positive);

  /* Taken at the first published press: the game's button-read counter at
     that moment, plus one, so that zero still means "no press yet". */
  unsigned long first_press_reads_ = 0;
  bool told_first_release_ = false;
  /* Which contact holds which control, and how many held controls hold each
     button. */
  std::set<std::pair<std::int64_t, std::uint32_t>> held_;
  std::map<std::string, int> holders_;
};

} // namespace x2::input

#endif /* X2_TOUCH_PAD_PUBLISHER_H */
