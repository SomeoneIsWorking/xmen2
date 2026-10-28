#include "popup_touch_layout.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {

using x2::input::PopupTouchInput;
using x2::input::PopupTouchLayout;
using x2::input::SceneBox;
using x2::presentation::RetailScenePlane;

int failures = 0;

void check(bool ok, const char *what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

/* CPopupDialog at 1280x720: ui/hud/dialog's box and the text box retail
   places in it. */
PopupTouchInput input(int entries, int footers) {
  PopupTouchInput in;
  in.plane =
      *RetailScenePlane::from_viewport(16.0F / 9.0F, 1.0F, 1.0F, 1280u, 720u);
  in.frame = {77.0F, 91.0F, 437.0F, 321.0F};
  in.text_left = 87.0F;
  in.text_top = 287.0F;
  in.line = 8.533F;
  in.finger = 0.1F * 720.0F * in.plane.height() / 720.0F;
  in.entries = entries;
  in.footers = footers;
  in.max_rows = 8;
  return in;
}

bool inside(const SceneBox &inner, const SceneBox &outer) {
  return inner.left >= outer.left && inner.right <= outer.right &&
         inner.bottom >= outer.bottom && inner.top <= outer.top;
}

float client_height(const PopupTouchInput &in, const SceneBox &box) {
  return box.height() * static_cast<float>(in.plane.client_height()) /
         in.plane.height();
}

void difficulty_fits_on_cards() {
  const PopupTouchInput in = input(3, 2);
  const PopupTouchLayout out = x2::input::layout_popup_touch(in);
  check(out.cards.cards.size() == 3u, "three entries make three cards");
  check(out.buttons.size() == 2u, "two prompts make two buttons");
  const SceneBox screen{in.plane.left(), 0.0F,
                        in.plane.left() + in.plane.width(), in.plane.height()};
  check(inside(out.frame, screen), "the frame stays on screen");
  for (const SceneBox &card : out.cards.cards) {
    check(inside(card, out.frame), "a card stays in the frame");
    check(client_height(in, card) >= 71.0F, "a card is a finger tall");
    for (const SceneBox &button : out.buttons) {
      check(card.bottom > button.top, "the cards sit above the buttons");
    }
  }
  check(out.buttons[0].right < out.buttons[1].left, "buttons do not overlap");
  check(!out.cards.more_above && !out.cards.more_below, "nothing to scroll");
}

void the_frame_holds_what_it_shows() {
  const PopupTouchLayout one = x2::input::layout_popup_touch(input(1, 1));
  const PopupTouchLayout three = x2::input::layout_popup_touch(input(3, 1));
  check(one.frame.height() < three.frame.height(),
        "a shorter page gets a shorter frame");
  check(three.frame.width() == one.frame.width(), "the width stays");
}

void a_long_list_scrolls() {
  PopupTouchInput in = input(11, 2);
  const PopupTouchLayout out = x2::input::layout_popup_touch(in);
  check(out.cards.rows < 11 &&
            out.cards.cards.size() == static_cast<std::size_t>(out.cards.rows),
        "the cards that fit are shown");
  check(out.cards.more_below && !out.cards.more_above,
        "the list continues below");
  check(x2::input::cards_scrolled(out.cards, 0, 2.0F * out.cards.pitch) == 2,
        "a drag of two pitches scrolls two cards");
  check(x2::input::cards_scrolled(out.cards, 0, 50.0F * out.cards.pitch) ==
            11 - out.cards.rows,
        "a long drag stops at the last card");
  check(x2::input::cards_scrolled(out.cards, 2, -50.0F * out.cards.pitch) == 0,
        "a long drag back stops at the first");
  in.first_shown = 99;
  const PopupTouchLayout last = x2::input::layout_popup_touch(in);
  check(last.cards.first == 11 - last.cards.rows,
        "a stale first shown is clamped");
  check(last.cards.more_above && !last.cards.more_below,
        "the list continues above");
}

void a_tap_finds_its_card() {
  const PopupTouchLayout out = x2::input::layout_popup_touch(input(3, 2));
  const SceneBox &second = out.cards.cards[1];
  check(x2::input::box_hit(out.cards.cards,
                           {0.5F * (second.left + second.right),
                            0.5F * (second.bottom + second.top)}) == 1,
        "the second card's middle hits it");
  check(x2::input::box_hit(out.cards.cards, {out.frame.left - 1.0F, 0.0F}) ==
            -1,
        "outside every card hits none");
}

/* The tutorial's switching hint: eleven lines of message and Continue. */
void a_message_wraps_above_its_button() {
  PopupTouchInput in = input(0, 1);
  const x2::input::PopupTouchTitle title = x2::input::popup_touch_title(in);
  in.title_scale = title.scale;
  in.title_height = 11.0F * in.line * title.scale;
  check(in.title_height <= title.room, "eleven lines fit at the nominal scale");
  const PopupTouchLayout out = x2::input::layout_popup_touch(in);
  check(std::fabs(out.text.width() - title.width) < 0.01F,
        "the text spans the wrap width");
  check(inside(out.text, out.frame), "the text stays in the frame");
  check(out.buttons.size() == 1u && out.buttons[0].top < out.text.bottom,
        "the button sits below the text");
  check(client_height(in, out.buttons[0]) >= 71.0F, "the button is a finger");
  const PopupTouchLayout one = x2::input::layout_popup_touch(input(0, 1));
  check(one.frame.height() < out.frame.height(),
        "a longer message gets a taller frame");
}

} // namespace

int main() {
  difficulty_fits_on_cards();
  the_frame_holds_what_it_shows();
  a_long_list_scrolls();
  a_tap_finds_its_card();
  a_message_wraps_above_its_button();
  if (failures != 0) {
    return EXIT_FAILURE;
  }
  std::puts("popup_touch_layout: ok");
  return EXIT_SUCCESS;
}
