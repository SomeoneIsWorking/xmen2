#include "text_touch_layout.hpp"

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

} // namespace

int main() {
  a_button_is_a_finger_tall_over_its_prompt();
  neighbouring_buttons_do_not_overlap();
  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
