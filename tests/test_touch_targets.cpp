#include "touch_targets.hpp"

#include <cstdio>
#include <cstdlib>

namespace {

using x2::input::SceneBox;
using x2::input::TouchTargets;

int failures = 0;

void check(bool ok, const char *what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

/* The shop's Buy footer: a 79x14 item at (82, 26) under a finger button. */
constexpr SceneBox kButton{72.0F, 7.0F, 171.0F, 45.0F};
constexpr x2::presentation::ScenePoint kItem{121.0F, 26.0F};

void a_press_on_a_shown_button_aims_at_its_item() {
  TouchTargets targets;
  targets.put({kButton, kItem});
  check(!targets.press({80.0F, 40.0F}), "a button drawn this frame is not hit");
  targets.next_frame();
  const auto aim = targets.press({80.0F, 40.0F});
  check(aim && aim->aim.x == kItem.x && aim->aim.z == kItem.z,
        "a press in the button is at its item");
  check(targets.pressed(kButton), "the pressed button is lit");
  targets.release({80.0F, 40.0F});
  check(!targets.pressed(kButton), "a lift unlights it");
  check(!targets.take_tap(kButton), "an item's button is no tap of the port's");
}

void a_tap_on_an_own_button_is_taken_once() {
  TouchTargets targets;
  targets.put({kButton, kItem, x2::input::TouchPress::own});
  targets.next_frame();
  check(targets.press({80.0F, 40.0F}).has_value(), "an own button is hit");
  targets.release({80.0F, 40.0F});
  check(targets.take_tap(kButton), "a lift inside it is a tap");
  check(!targets.take_tap(kButton), "taken once");
  targets.press({80.0F, 40.0F});
  targets.release({300.0F, 40.0F});
  check(!targets.take_tap(kButton), "a lift outside it is no tap");
  targets.press({80.0F, 40.0F});
  targets.release({80.0F, 40.0F});
  targets.next_frame();
  check(!targets.take_tap(kButton), "a tap no draw took is dropped");
}

void a_press_off_every_button_is_the_finger() {
  TouchTargets targets;
  targets.put({kButton, kItem});
  targets.next_frame();
  check(!targets.press({200.0F, 26.0F}),
        "a press beside the button is not aimed");
  check(!targets.pressed(kButton), "and lights nothing");
}

void a_button_not_drawn_again_is_gone() {
  TouchTargets targets;
  targets.put({kButton, kItem});
  targets.next_frame();
  targets.next_frame();
  check(!targets.press({80.0F, 40.0F}), "a closed menu's button is not hit");
}

/* Two main menu rows 36 apart, each a finger (38.4) tall. */
void overlapping_buttons_split_at_their_middle() {
  constexpr SceneBox kUpper{18.0F, 230.8F, 187.0F, 269.2F};
  constexpr SceneBox kLower{18.0F, 194.8F, 187.0F, 233.2F};
  constexpr x2::presentation::ScenePoint kUpperItem{93.0F, 250.0F};
  constexpr x2::presentation::ScenePoint kLowerItem{93.0F, 214.0F};
  TouchTargets targets;
  targets.put({kUpper, kUpperItem});
  targets.put({kLower, kLowerItem});
  targets.next_frame();
  const auto upper = targets.press({100.0F, 232.5F});
  check(upper && upper->aim.z == kUpperItem.z,
        "a press above the middle of an overlap is the upper row");
  check(targets.pressed(kUpper) && !targets.pressed(kLower),
        "and lights only it");
  const auto lower = targets.press({100.0F, 231.5F});
  check(lower && lower->aim.z == kLowerItem.z,
        "a press below the middle is the lower row");
}

void a_hover_is_taken_once() {
  TouchTargets targets;
  check(!targets.take_hover(), "no hover asked for");
  targets.hover(kItem);
  const auto hover = targets.take_hover();
  check(hover && hover->x == kItem.x && hover->z == kItem.z,
        "the hover is at its point");
  check(!targets.take_hover(), "taken once");
}

} // namespace

int main() {
  a_press_on_a_shown_button_aims_at_its_item();
  a_tap_on_an_own_button_is_taken_once();
  a_press_off_every_button_is_the_finger();
  a_button_not_drawn_again_is_gone();
  overlapping_buttons_split_at_their_middle();
  a_hover_is_taken_once();
  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
