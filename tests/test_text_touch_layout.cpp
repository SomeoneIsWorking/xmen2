#include "text_touch_layout.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {

using x2::input::SceneBox;

int failures = 0;

void check(bool ok, const char *what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

/* The shop's footers at 1280x720: 79x14 boxes 93 apart, a finger 38.4. */
constexpr float kFinger = 38.4F;
constexpr SceneBox kBuy{82.0F, 21.0F, 161.0F, 35.0F};
constexpr SceneBox kAccept{175.0F, 21.0F, 254.0F, 35.0F};

void a_button_is_a_finger_tall_over_its_prompt() {
  const SceneBox button = x2::input::text_button(kBuy, kFinger);
  check(button.height() == kFinger, "a button is a finger tall");
  check(button.left < kBuy.left && button.right > kBuy.right,
        "a button is wider than its prompt");
  check(button.left + button.right == kBuy.left + kBuy.right &&
            button.bottom + button.top == kBuy.bottom + kBuy.top,
        "a button is centred on its prompt");
}

void neighbouring_buttons_do_not_overlap() {
  const SceneBox buy = x2::input::text_button(kBuy, kFinger);
  const SceneBox accept = x2::input::text_button(kAccept, kFinger);
  check(buy.right <= accept.left, "Buy ends before Accept starts");
}

/* The main menu's New Game: its text over its bar from 18 to 187. */
void a_row_button_spans_its_bar_a_finger_tall() {
  constexpr SceneBox kNewGame{29.0F, 243.0F, 158.0F, 257.0F};
  const SceneBox button =
      x2::input::row_button(kNewGame, 18.0F, 187.0F, kFinger);
  check(button.left == 18.0F && button.right == 187.0F,
        "a row button spans its bar");
  check(std::fabs(button.height() - kFinger) < 0.001F &&
            button.bottom + button.top == kNewGame.bottom + kNewGame.top,
        "a finger tall about the row's text");
}

} // namespace

int main() {
  a_button_is_a_finger_tall_over_its_prompt();
  neighbouring_buttons_do_not_overlap();
  a_row_button_spans_its_bar_a_finger_tall();
  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
