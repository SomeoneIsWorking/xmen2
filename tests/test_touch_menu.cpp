/*
 * The touch menu's pure parts through the shipping code: the view built from a
 * retail menu snapshot, its layout and hit test, what a tap delivers to the
 * game, and the scene-plane mapping a delivered click crosses.
 */
#include "../src/input/touch_menu.hpp"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

int failures;
int checks;

void check(bool ok, const std::string &what) {
  ++checks;
  if (!ok) {
    ++failures;
    std::printf("FAIL: %s\n", what.c_str());
  }
}

using x2::input::TouchAction;
using x2::input::TouchMenu;
using x2::input::TouchMenuButton;
using x2::input::TouchMenuDelivery;
using x2::input::TouchMenuLayout;
using x2::input::TouchMenuPart;
using x2::input::TouchMenuView;
using x2::menu::MenuItem;
using x2::menu::MenuSnapshot;
using x2::presentation::RetailScenePlane;

constexpr float kAspect = 1280.0F / 720.0F;

RetailScenePlane plane_1280x720() {
  return *RetailScenePlane::from_viewport(kAspect, 1.0F, 1.0F, 1280u, 720u);
}

MenuItem item(unsigned slot, const char *name, const char *label, int left,
              int top, int right, int bottom) {
  MenuItem out;
  out.slot = slot;
  out.name = name;
  out.label = label;
  out.rect = {left, top, right, bottom};
  out.flags = x2::menu::kItemEnabled;
  out.navigable = true;
  return out;
}

/* The retail Options menu as GET /menu read it on a real boot. */
MenuSnapshot options_menu() {
  MenuSnapshot menu;
  menu.address = 0x27128534u;
  menu.name = "options";
  menu.menu_class = "CMenuOptions";
  MenuItem volume =
      item(0, "label_effects_volume", "Effects Volume", 27, 277, 114, 291);
  volume.left_command = "setdecrement sfxvolume";
  volume.right_command = "setincrement sfxvolume";
  volume.fill = 1.0F;
  menu.items.push_back(volume);
  MenuItem combat =
      item(1, "label_combat_music", "Combat Music", 27, 218, 114, 232);
  combat.use_command = "setincrement music";
  combat.value_item = 4;
  menu.items.push_back(combat);
  MenuItem accept = item(2, "label_accept", "Accept", 29, 66, 158, 80);
  accept.use_command = "saveoptions";
  menu.items.push_back(accept);
  menu.items.push_back(
      item(3, "desctext1", "~05$MENU_BACK Back", 29, 21, 108, 35));
  menu.items.push_back(item(4, "combat_music", "On", 117, 218, 174, 232));
  MenuItem hidden =
      item(5, "desctext2", "~05$MENU_ACCEPT Change", 122, 21, 201, 35);
  hidden.flags |= x2::menu::kItemHidden;
  menu.items.push_back(hidden);
  menu.items.push_back(
      item(6, "desctext4", "~05$MENU_OK Advanced Options", 308, 21, 387, 35));
  menu.items.push_back(item(7, "desctext5", "~05", 401, 21, 480, 35));
  menu.items.push_back(item(8, "label_options", "Options", 29, 320, 158, 334));
  menu.rows = {0, 1, 2};
  menu.focused = 0;
  return menu;
}

X2LayoutViewport viewport_1280x720() {
  X2LayoutViewport viewport{};
  viewport.width = 1280.0F;
  viewport.height = 720.0F;
  return viewport;
}

const TouchMenuButton *find(const TouchMenuLayout &layout, TouchMenuPart part,
                            int index) {
  for (const TouchMenuButton &button : layout.buttons) {
    if (button.part == part && button.index == index) {
      return &button;
    }
  }
  return nullptr;
}

std::vector<TouchMenuDelivery> tap(TouchMenu &menu, const X2Rect &rect,
                                   std::uint64_t now) {
  const lucent::touch::Point at{0.5F * (rect.left + rect.right),
                                0.5F * (rect.top + rect.bottom)};
  menu.contact(1, at, lucent::touch::Phase::began, now);
  return menu.contact(1, at, lucent::touch::Phase::ended, now);
}

void retail_text_reads_as_a_player_sees_it() {
  using x2::input::touch_menu_text;
  check(touch_menu_text("~05$MENU_BACK Back") == "Back",
        "a footer's escape and token are removed");
  check(touch_menu_text("~05$MENU_OK Advanced Options") == "Advanced Options",
        "and its words kept");
  check(touch_menu_text("r i s e   o f   a p o c a l y p s e") ==
            "rise of apocalypse",
        "a letter-spaced title closes up");
  check(touch_menu_text("  new   game ") == "new game", "whitespace collapses");
  check(touch_menu_text("~05").empty(), "an escape alone is no text");
}

void the_view_carries_the_rows_values_and_footer() {
  const auto view =
      x2::input::build_touch_menu_view(options_menu(), plane_1280x720());
  check(view.has_value(), "Options is a menu the touch menu replaces");
  if (!view) {
    return;
  }
  check(view->title == "Options", "the title is the menu's own label");
  check(view->rows.size() == 3u, "every navigable row is offered");
  check(view->rows[0].steps && !view->rows[0].clicks && view->rows[0].fill &&
            *view->rows[0].fill == 1.0F,
        "a volume row steps and shows its bar");
  check(view->rows[1].clicks && view->rows[1].value == "On",
        "a toggle row clicks and shows its paired value");
  check(view->focused_row == 0, "the game's focus is carried");
  check(view->footers.size() == 2u,
        "visible $MENU_ desctext items are footers");
  check(view->footers.size() == 2u && view->footers[0].token == "$MENU_BACK" &&
            view->footers[0].label == "Back" &&
            view->footers[1].token == "$MENU_OK",
        "each footer names its token and the game's words");

  MenuSnapshot loading = options_menu();
  loading.menu_class = "CMenuLoading";
  check(!x2::input::build_touch_menu_view(loading, plane_1280x720()),
        "a menu class the touch menu does not replace keeps the retail screen");
  MenuSnapshot popup = options_menu();
  popup.popup_up = true;
  check(!x2::input::build_touch_menu_view(popup, plane_1280x720()),
        "a popup over the menu keeps the retail screen");
  MenuSnapshot empty = options_menu();
  empty.rows.clear();
  check(!x2::input::build_touch_menu_view(empty, plane_1280x720()),
        "a menu with no selectable row keeps the retail screen");
}

/* CMenuTeam's party screen as GET /menu read it in the jungle: four hero
   summaries, Cyclops selected, and the nav chain on the potion icons. */
MenuSnapshot team_menu(std::uint32_t mode) {
  MenuSnapshot menu;
  menu.address = 0x27128964u;
  menu.name = "team";
  menu.menu_class = "CMenuTeam";
  menu.mode = mode;
  menu.items.push_back(item(58, "item_health", "", 15, -77, 61, -46));
  const char *heroes[] = {"Magneto", "Cyclops", "Wolverine", "Storm"};
  for (int i = 0; i < 4; ++i) {
    const std::string name = "char_summary0" + std::to_string(i + 1);
    MenuItem hero = item(static_cast<unsigned>(52 + i), "", heroes[i], 364,
                         313 - 80 * i, 479, 371 - 80 * i);
    hero.name = name;
    if (i == 1) {
      hero.flags |= x2::menu::kItemFocusLit;
    }
    menu.items.push_back(hero);
  }
  menu.items.push_back(
      item(38, "desctext4", "~05$MENU_OTHER Details", 268, 21, 347, 35));
  menu.items.push_back(
      item(39, "desctext5", "~05$MENU_OK Accept", 401, 21, 480, 35));
  menu.rows = {0};
  menu.focused = 0;
  return menu;
}

void the_team_party_is_its_heroes() {
  const RetailScenePlane plane = plane_1280x720();
  const MenuSnapshot menu = team_menu(0u);
  const auto view = x2::input::build_touch_menu_view(menu, plane);
  check(view.has_value(), "the team's party screen is replaced");
  if (!view) {
    return;
  }
  check(view->rows.size() == 4u && view->rows[0].label == "Magneto" &&
            view->rows[3].label == "Storm",
        "one row per hero summary, in party order, not the nav chain");
  check(view->focused_row == 1 && view->rows[1].focused,
        "the lit summary is the selected hero");
  bool clicks = true;
  bool inside = true;
  for (std::size_t i = 0; i < view->rows.size(); ++i) {
    clicks = clicks && view->rows[i].clicks;
    const auto &rect = menu.items[i + 1u].rect;
    const auto scene = plane.to_scene(view->rows[i].click);
    inside = inside && scene.x >= static_cast<float>(rect.left) &&
             scene.x < static_cast<float>(rect.right) &&
             scene.z >= static_cast<float>(rect.top) &&
             scene.z < static_cast<float>(rect.bottom);
  }
  check(clicks, "a hero is chosen by the game's own click on its summary");
  check(inside, "each click lands inside the summary CMenuTeam::onMouse tests");
  check(view->footers.size() == 2u && view->footers[0].label == "Details" &&
            view->footers[1].token == "$MENU_OK",
        "the party's Details and Accept footers");
  check(!x2::input::build_touch_menu_view(team_menu(2u), plane),
        "a hero's detail tabs keep the retail screen");
  check(!x2::input::build_touch_menu_view(team_menu(1u), plane),
        "the roster keeps the retail screen");
  MenuSnapshot unread = team_menu(0u);
  unread.mode.reset();
  check(!x2::input::build_touch_menu_view(unread, plane),
        "a team menu whose mode was not read keeps the retail screen");

  TouchMenu touch;
  touch.set_viewport(viewport_1280x720());
  touch.set_view(view, 0u);
  const auto out =
      tap(touch, find(touch.layout(), TouchMenuPart::row, 3)->rect, 10u);
  check(out.size() == 1u && out[0].kind == TouchMenuDelivery::Kind::click &&
            out[0].at.x == view->rows[3].click.x &&
            out[0].at.y == view->rows[3].click.y,
        "a tap on Storm is a click on Storm's summary");
}

/* A click delivered at a row's client point lands inside the box
   CMenuItem::onMouse tests, after FUN_005f9eb0's own mapping. */
void a_delivered_click_lands_in_the_hit_box() {
  const MenuSnapshot menu = options_menu();
  for (const float aspect : {kAspect, 4.0F / 3.0F, 2400.0F / 1080.0F}) {
    for (const unsigned width : {640u, 1280u, 1920u}) {
      const unsigned height = static_cast<unsigned>(
          std::lround(static_cast<float>(width) / aspect));
      const auto plane =
          RetailScenePlane::from_viewport(aspect, 1.0F, 1.0F, width, height);
      check(plane.has_value(), "the plane exists");
      if (!plane) {
        continue;
      }
      const auto view = x2::input::build_touch_menu_view(menu, *plane);
      if (!view) {
        check(false, "the view exists");
        continue;
      }
      for (std::size_t i = 0; i < view->rows.size(); ++i) {
        const auto &rect = menu.items[menu.rows[i]].rect;
        const auto scene = plane->to_scene(view->rows[i].click);
        check(scene.x >= static_cast<float>(rect.left) &&
                  scene.x < static_cast<float>(rect.right) &&
                  scene.z >= static_cast<float>(rect.top) &&
                  scene.z < static_cast<float>(rect.bottom),
              "row " + std::to_string(i) + " click is inside its hit box at " +
                  std::to_string(width) + "x" + std::to_string(height));
      }
    }
  }
  check(!RetailScenePlane::from_viewport(kAspect, 0.0F, 1.0F, 1280u, 720u),
        "a viewport with no width is no plane");
  check(!RetailScenePlane::from_viewport(kAspect, 1.0F, 1.0F, 0u, 720u),
        "a backbuffer with no width is no plane");
}

void the_layout_is_finger_sized_and_inside_the_safe_area() {
  auto view =
      *x2::input::build_touch_menu_view(options_menu(), plane_1280x720());
  for (int i = 0; i < 9; ++i) {
    view.rows.push_back(view.rows[1]);
    view.rows.back().slot = 100u + static_cast<unsigned>(i);
  }
  X2LayoutViewport viewport = viewport_1280x720();
  viewport.safe_left = 40.0F;
  viewport.safe_bottom = 30.0F;
  const TouchMenuLayout layout =
      x2::input::layout_touch_menu(view, viewport, 0.0F);
  bool sized = true;
  bool inside = true;
  for (const TouchMenuButton &button : layout.buttons) {
    sized = sized && button.rect.right - button.rect.left >= 48.0F &&
            button.rect.bottom - button.rect.top >= 48.0F;
    inside = inside && button.rect.left >= viewport.safe_left &&
             button.rect.right <= viewport.width - viewport.safe_right;
    if (button.part == TouchMenuPart::footer) {
      inside = inside &&
               button.rect.bottom <= viewport.height - viewport.safe_bottom;
    }
  }
  check(sized, "every button is at least the 48-pixel touch minimum");
  check(inside, "every button is inside the safe area");
  check(find(layout, TouchMenuPart::step_left, 0) &&
            find(layout, TouchMenuPart::step_right, 0) &&
            !find(layout, TouchMenuPart::step_left, 1),
        "only a row with left/right gets step buttons");
  check(layout.max_scroll > 0.0F, "twelve rows scroll on a 720-pixel screen");
  const TouchMenuButton *last =
      find(layout, TouchMenuPart::row, static_cast<int>(view.rows.size()) - 1);
  check(last && !layout.hit(0.5F * (last->rect.left + last->rect.right),
                            0.5F * (last->rect.top + last->rect.bottom)),
        "a row scrolled out of the list is not hit");
  const TouchMenuLayout end =
      x2::input::layout_touch_menu(view, viewport, 1.0e6F);
  check(end.scroll == end.max_scroll, "scroll clamps to the last row");
  const TouchMenuButton *footer = find(layout, TouchMenuPart::footer, 0);
  check(footer && layout.hit(0.5F * (footer->rect.left + footer->rect.right),
                             0.5F * (footer->rect.top + footer->rect.bottom)) ==
                      static_cast<std::size_t>(footer - layout.buttons.data()),
        "a footer button is hit at its centre");
}

void a_menu_without_footers_gives_their_band_to_the_list() {
  auto view =
      *x2::input::build_touch_menu_view(options_menu(), plane_1280x720());
  const TouchMenuLayout with =
      x2::input::layout_touch_menu(view, viewport_1280x720(), 0.0F);
  view.footers.clear();
  const TouchMenuLayout without =
      x2::input::layout_touch_menu(view, viewport_1280x720(), 0.0F);
  check(without.list.bottom > with.footer.top &&
            !find(without, TouchMenuPart::footer, 0),
        "a menu without footers lists rows down to the bottom margin");
}

void a_tap_delivers_the_games_own_input() {
  const RetailScenePlane plane = plane_1280x720();
  MenuSnapshot snapshot = options_menu();
  /* Row 2 as CMenuMain's command-less rows: the class acts on its focus. */
  snapshot.items[2].use_command.clear();
  TouchMenu menu;
  menu.set_viewport(viewport_1280x720());
  menu.set_view(x2::input::build_touch_menu_view(snapshot, plane), 0u);
  check(menu.shown(), "the touch menu is shown over Options");
  const TouchMenuView view = *x2::input::build_touch_menu_view(snapshot, plane);

  auto out = tap(menu, find(menu.layout(), TouchMenuPart::row, 1)->rect, 10u);
  check(out.size() == 1u && out[0].kind == TouchMenuDelivery::Kind::click &&
            out[0].at.x == view.rows[1].click.x &&
            out[0].at.y == view.rows[1].click.y,
        "a row with a command is clicked where the game hit-tests it");

  out = tap(menu, find(menu.layout(), TouchMenuPart::footer, 0)->rect, 20u);
  check(out.size() == 1u && out[0].kind == TouchMenuDelivery::Kind::click &&
            out[0].at.x == view.footers[0].click.x,
        "a footer is clicked on the desctext item carrying its token");

  out = tap(menu, find(menu.layout(), TouchMenuPart::step_right, 0)->rect, 30u);
  check(out.size() == 1u && out[0].kind == TouchMenuDelivery::Kind::pad &&
            out[0].button == TouchAction::MenuRight,
        "a step on the focused row is the menu pad's Right");

  /* Row 2 has no command; the game's focus is on row 0, and Up through the
     wrap is the shorter way. */
  out = tap(menu, find(menu.layout(), TouchMenuPart::row, 2)->rect, 40u);
  check(out.size() == 1u && out[0].button == TouchAction::MenuUp,
        "a row reached by walking starts with one press the shorter way");
  out = menu.set_view(x2::input::build_touch_menu_view(snapshot, plane), 50u);
  check(out.empty(), "no second press until the game's focus moved");
  out = menu.set_view(x2::input::build_touch_menu_view(snapshot, plane), 700u);
  check(out.size() == 1u && out[0].button == TouchAction::MenuUp,
        "a press the game did not answer is sent again");
  snapshot.focused = 2;
  out = menu.set_view(x2::input::build_touch_menu_view(snapshot, plane), 710u);
  check(out.size() == 1u && out[0].button == TouchAction::MenuA,
        "and A once the focus is on the row");
  out = menu.set_view(x2::input::build_touch_menu_view(snapshot, plane), 720u);
  check(out.empty(), "the walk is over");

  /* From row 2, row 0 is one Down through the wrap. */
  out = tap(menu, find(menu.layout(), TouchMenuPart::step_left, 0)->rect, 800u);
  check(out.size() == 1u && out[0].button == TouchAction::MenuDown,
        "a step on another row walks there first");
  snapshot.focused = 0;
  out = menu.set_view(x2::input::build_touch_menu_view(snapshot, plane), 810u);
  check(out.size() == 1u && out[0].button == TouchAction::MenuLeft,
        "then steps it with Left");

  menu.set_view(std::nullopt, 820u);
  check(!menu.shown(), "no menu to replace hides it");
  out = tap(menu, X2Rect{600.0F, 300.0F, 620.0F, 320.0F}, 830u);
  check(out.empty(), "and a hidden touch menu delivers nothing");
}

void a_drag_scrolls_and_does_not_press() {
  auto view =
      *x2::input::build_touch_menu_view(options_menu(), plane_1280x720());
  for (int i = 0; i < 9; ++i) {
    view.rows.push_back(view.rows[1]);
    view.rows.back().slot = 100u + static_cast<unsigned>(i);
  }
  TouchMenu menu;
  menu.set_viewport(viewport_1280x720());
  menu.set_view(view, 0u);
  const X2Rect row = find(menu.layout(), TouchMenuPart::row, 3)->rect;
  const float x = 0.5F * (row.left + row.right);
  const float y = 0.5F * (row.top + row.bottom);
  menu.contact(7, {x, y}, lucent::touch::Phase::began, 0u);
  menu.contact(8, {x, y + 5.0F}, lucent::touch::Phase::began, 0u);
  menu.contact(7, {x, y - 120.0F}, lucent::touch::Phase::moved, 0u);
  const auto out =
      menu.contact(7, {x, y - 120.0F}, lucent::touch::Phase::ended, 0u);
  check(out.empty(), "a drag presses nothing");
  check(menu.layout().scroll > 100.0F, "a drag up scrolls the list down");
  check(menu.contact(8, {x, y}, lucent::touch::Phase::ended, 0u).empty(),
        "a second finger never acts");
}

} // namespace

int main() {
  retail_text_reads_as_a_player_sees_it();
  the_view_carries_the_rows_values_and_footer();
  a_delivered_click_lands_in_the_hit_box();
  the_layout_is_finger_sized_and_inside_the_safe_area();
  a_menu_without_footers_gives_their_band_to_the_list();
  a_tap_delivers_the_games_own_input();
  the_team_party_is_its_heroes();
  a_drag_scrolls_and_does_not_press();
  if (failures) {
    std::printf("touch_menu: %d of %d check(s) failed\n", failures, checks);
    return 1;
  }
  std::printf("touch_menu: %d check(s) passed\n", checks);
  return 0;
}
