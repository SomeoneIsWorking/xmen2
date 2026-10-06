/* The cinematic skip a finger asks for: the offer, the request and the
   button that makes it. */
#include "../src/input/cutscene_skip.h"
#include "../src/input/touch_skip_button.h"

#include <cstdio>

namespace {

int failures = 0;

void check(const char *what, bool ok) {
  if (!ok) {
    std::printf("FAIL: %s\n", what);
    failures++;
  }
}

constexpr X2LayoutViewport kPhone{2728.0F, 1264.0F, 96.0F, 0.0F, 96.0F, 0.0F};

float centre_x(X2Rect rect) { return (rect.left + rect.right) * 0.5F; }
float centre_y(X2Rect rect) { return (rect.top + rect.bottom) * 0.5F; }

} // namespace

int main() {
  using lucent::touch::Phase;
  using x2::input::SkipButton;

  /* The offer and the request: one request per offer, taken once, and
     nothing held across a withdrawn offer. */
  {
    x2_cutscene_skip_reset();
    check("nothing is offered before the player says so",
          !x2_cutscene_skip_available());
    check("a request with nothing offered is refused",
          !x2_cutscene_skip_request());
    check("and there is nothing to take", !x2_cutscene_skip_take_request());
    x2_cutscene_skip_offer(1);
    check("an offered skip can be requested", x2_cutscene_skip_request());
    check("the player takes it", x2_cutscene_skip_take_request());
    check("exactly once", !x2_cutscene_skip_take_request());
    x2_cutscene_skip_request();
    x2_cutscene_skip_offer(0);
    x2_cutscene_skip_offer(1);
    check("a withdrawn offer drops the request nobody took, so a late tap "
          "cannot skip the next cinematic",
          !x2_cutscene_skip_take_request());
    const X2CutsceneSkipCounts counts = x2_cutscene_skip_counts();
    check("the counts say so",
          counts.accepted == 2 && counts.refused == 1 && counts.taken == 1);
  }

  /* Placement: top-right, inside the safe area, on screen. */
  {
    const X2Rect rect = SkipButton::place(kPhone);
    check("the button is inside the right safe inset",
          rect.right <= kPhone.width - kPhone.safe_right);
    check("and in the top half", rect.bottom < kPhone.height * 0.5F);
    check("in the right half", rect.left > kPhone.width * 0.5F);
    check("and has area", rect.right > rect.left && rect.bottom > rect.top);
  }

  /* The press: only while offered, only inside, once per contact, and the
     contact is held until it lifts. */
  {
    x2_cutscene_skip_reset();
    SkipButton button;
    const X2Rect rect = SkipButton::place(kPhone);
    const float x = centre_x(rect);
    const float y = centre_y(rect);
    check("with no skip offered the button takes nothing",
          !button.press(1, x, y, Phase::began, kPhone));
    x2_cutscene_skip_offer(1);
    check("a contact elsewhere is not the button's",
          !button.press(2, 10.0F, 10.0F, Phase::began, kPhone));
    check("a contact that did not start on it is not the button's",
          !button.press(3, x, y, Phase::moved, kPhone));
    check("nothing has been requested yet", !x2_cutscene_skip_take_request());
    check("a contact that lands on it is taken",
          button.press(4, x, y, Phase::began, kPhone));
    check("and it requested the skip", x2_cutscene_skip_take_request());
    check("the finger is held", button.held());
    check("its motion stays with the button",
          button.press(4, 10.0F, 10.0F, Phase::moved, kPhone));
    check("and requests nothing more", !x2_cutscene_skip_take_request());
    check("a second finger is not the held one",
          !button.press(5, 10.0F, 10.0F, Phase::began, kPhone));
    check("lifting is the button's too",
          button.press(4, x, y, Phase::ended, kPhone));
    check("and lets go", !button.held());
  }

  if (failures == 0) {
    std::printf("touch skip button: all checks passed\n");
  }
  return failures == 0 ? 0 : 1;
}
