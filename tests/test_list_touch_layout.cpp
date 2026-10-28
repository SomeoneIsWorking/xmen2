#include "list_touch_layout.hpp"

#include <cstdio>
#include <cstdlib>

namespace {

using x2::input::ListTouchInput;
using x2::input::ListTouchLayout;
using x2::input::SceneBox;

int failures = 0;

void check(bool ok, const char *what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

/* The shop's list at 1280x720, where a finger is 38.4 scene units. */
ListTouchInput shop(int entries, int top) {
  ListTouchInput in;
  in.box = {215.0F, 191.0F, 482.0F, 377.0F};
  in.line = 8.533F;
  in.finger = 38.4F;
  in.entries = entries;
  in.top = top;
  in.icons = true;
  in.max_rows = 10;
  return in;
}

void cards_fill_the_box() {
  const ListTouchLayout out = x2::input::layout_list_touch(shop(13, 0));
  const x2::input::CardStack &stack = out.cards;
  check(stack.rows == 4, "the shop's box holds four finger cards");
  check(stack.cards.size() == 4u, "four of thirteen entries are shown");
  for (const SceneBox &card : stack.cards) {
    check(card.left == 215.0F && card.right == 482.0F, "a card spans the box");
    check(card.bottom >= 191.0F && card.top <= 377.0F,
          "a card stays in the box");
    check(card.height() >= 38.0F, "a card is a finger tall");
  }
  check(stack.cards[0].top == 377.0F, "the first card starts at the top");
  check(stack.more_below && !stack.more_above, "the list continues below");
  check(out.text_inset > out.icon, "text starts after the icon");
  check(out.text_scale > 1.5F, "text is larger than retail's");
}

void a_late_top_is_clamped() {
  const ListTouchLayout out = x2::input::layout_list_touch(shop(13, 12));
  check(out.cards.first == 9, "the last card is filled");
  check(out.cards.more_above && !out.cards.more_below,
        "the list continues above");
}

void a_short_list_shows_all() {
  ListTouchInput in = shop(2, 0);
  in.icons = false;
  const ListTouchLayout out = x2::input::layout_list_touch(in);
  check(out.cards.cards.size() == 2u, "two entries make two cards");
  check(!out.cards.more_above && !out.cards.more_below, "nothing to scroll");
  check(out.icon == 0.0F, "no icon column without icons");
}

void nested_lists_take_turns_in_the_outer_box() {
  using x2::input::ListPlace;
  using x2::input::nested_list_area;
  using x2::input::SceneBox;
  const SceneBox slots{215.0F, 112.0F, 482.0F, 287.0F};
  const SceneBox pieces{215.0F, 131.0F, 482.0F, 204.0F};
  const auto shown = nested_list_area({slots, true}, {{pieces, false}});
  check(shown && shown->bottom == slots.bottom && shown->top == slots.top,
        "the focused slots fill their whole box");
  check(!nested_list_area({pieces, false}, {{slots, true}}),
        "and the pieces are not drawn");
  const auto pieces_shown = nested_list_area({pieces, true}, {{slots, false}});
  check(pieces_shown && pieces_shown->bottom == slots.bottom &&
            pieces_shown->top == slots.top,
        "the focused pieces fill the slots' box");
  check(nested_list_area({slots, false}, {{pieces, false}}).has_value() &&
            !nested_list_area({pieces, false}, {{slots, false}}),
        "with neither focused the outer list shows");
  const SceneBox beside{500.0F, 131.0F, 600.0F, 204.0F};
  const auto kept = nested_list_area({slots, false}, {{beside, true}});
  check(kept && kept->bottom == slots.bottom && kept->top == slots.top,
        "a list beside it changes nothing");
}

void only_native_menus_lay_out_lists() {
  using x2::input::list_has_touch_layout;
  using x2::menu::ItemClass;
  check(list_has_touch_layout(ItemClass::list_box, "CMenuShop"),
        "the shop's list box is laid out");
  check(list_has_touch_layout(ItemClass::list_codex, "CMenuCodex"),
        "the codex's list is laid out");
  check(list_has_touch_layout(ItemClass::list_box, "CMenuTeam"),
        "the team's gear lists are laid out");
  check(!list_has_touch_layout(ItemClass::list_box, "CMenuHost"),
        "a list in a menu not native on touch is left to retail");
  check(!list_has_touch_layout(ItemClass::text, "CMenuShop"),
        "a text item is not a list");
}

} // namespace

int main() {
  cards_fill_the_box();
  a_late_top_is_clamped();
  a_short_list_shows_all();
  only_native_menus_lay_out_lists();
  nested_lists_take_turns_in_the_outer_box();
  if (failures != 0) {
    std::fprintf(stderr, "%d failure(s)\n", failures);
    return EXIT_FAILURE;
  }
  std::puts("list touch layout: ok");
  return EXIT_SUCCESS;
}
