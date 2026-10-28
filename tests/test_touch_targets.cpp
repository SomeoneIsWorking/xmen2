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
  check(aim && aim->x == kItem.x && aim->z == kItem.z,
        "a press in the button is at its item");
  check(targets.pressed(kButton), "the pressed button is lit");
  targets.release();
  check(!targets.pressed(kButton), "a lift unlights it");
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

} // namespace

int main() {
  a_press_on_a_shown_button_aims_at_its_item();
  a_press_off_every_button_is_the_finger();
  a_button_not_drawn_again_is_gone();
  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
