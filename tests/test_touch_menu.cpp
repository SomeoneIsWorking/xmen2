/*
 * The touch menu's pure parts through the shipping code: the view built from a
 * retail menu snapshot, its layout and hit test, what a tap delivers to the
 * game, and the scene-plane mapping a delivered click crosses.
 */
#include "touch_menu_fixture.hpp"

#include "../src/input/touch_menu_parts.hpp"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

using namespace x2::test::touch_menu;

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

/* A touch menu view of Options' rows and footers, for the layout and the
   taps; Options itself is drawn natively. */
TouchMenuView options_view(const MenuSnapshot &menu,
                           const RetailScenePlane &plane) {
  TouchMenuView view = x2::input::start_menu_view(menu);
  for (const int index : menu.rows) {
    const MenuItem &source = menu.items[static_cast<std::size_t>(index)];
    TouchMenuRow row;
    row.label = x2::input::touch_menu_text(source.label);
    row.fill = source.fill;
    row.clicks = !source.use_command.empty();
    row.steps = source.has_left_right();
    row.focused = index == menu.focused;
    row.slot = source.slot;
    row.click = x2::input::menu_item_centre(source.rect, plane);
    if (row.focused) {
      view.focused_row = static_cast<int>(view.rows.size());
    }
    view.rows.push_back(std::move(row));
  }
  x2::input::append_menu_footers(menu, plane, &view);
  return view;
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
  check(touch_menu_text("Recovers 33% of max $HP with a chance") ==
            "Recovers 33% of max HP with a chance",
        "a stat token draws as its name, as the retail shop shows it");
  check(touch_menu_text("~02Limit:~~ 10") == "Limit: 10",
        "the style reset escape is removed");
}

/* The online region list as GET /menu read it from openmenu region. */
MenuSnapshot region_menu(const std::vector<std::string> &regions,
                         int selected) {
  MenuSnapshot menu;
  menu.address = 0x27128974u;
  menu.name = "region";
  menu.menu_class = "CMenuRegion";
  menu.items.push_back(
      item(5, "desctext1", "~05$MENU_BACK Back", 29, 21, 108, 35));
  menu.items.push_back(item(6, "desctext2", "~05 ", 122, 21, 201, 35));
  menu.items.push_back(
      item(7, "desctext3", "~05$MENU_SUBTRACT Refresh", 215, 21, 294, 35));
  menu.items.push_back(
      item(9, "desctext5", "~05$MENU_ACCEPT Select", 381, 21, 460, 35));
  menu.items.push_back(item(13, "data_name", "", 29, 290, 480, 306));
  menu.items.push_back(
      item(19, "text_title", "Region Menu", 31, 351, 160, 365));
  MenuItem list = item(27, "text_list", "", 29, 94, 480, 299);
  x2::menu::ListBoxState box;
  for (const std::string &region : regions) {
    box.entries.push_back(region);
    box.columns.push_back({std::to_string(region.size())});
  }
  box.selected = selected;
  box.visible_rows = 25;
  box.row_height = 8;
  box.hit = {29, 94, 480, 299};
  list.list_box = box;
  menu.items.push_back(list);
  return menu;
}

void main_options_and_the_pda_are_native() {
  for (const char *native : {"CMenuMain", "CMenuOptions", "CMenuPDA"}) {
    MenuSnapshot menu = options_menu();
    menu.menu_class = native;
    check(!x2::input::build_touch_menu_view(menu, plane_1280x720()),
          std::string(native) + " is drawn and touched natively");
  }
  for (const char *native : {"CMenuShop", "CMenuCodex", "CMenuReviewPaths",
                             "CMenuDangerRoom", "CMenuTrivia", "CMenuRegion"}) {
    MenuSnapshot menu = region_menu({"Americas", "Europe"}, 0);
    menu.menu_class = native;
    check(!x2::input::build_touch_menu_view(menu, plane_1280x720()),
          std::string(native) + " and its list box are drawn natively");
  }
  MenuSnapshot loading = region_menu({"Americas"}, 0);
  loading.menu_class = "CMenuLoading";
  check(!x2::input::build_touch_menu_view(loading, plane_1280x720()),
        "a menu class the touch menu does not replace keeps the retail screen");
  MenuSnapshot popup = region_menu({"Americas"}, 0);
  popup.popup_up = true;
  check(!x2::input::build_touch_menu_view(popup, plane_1280x720()),
        "a popup over the menu keeps the retail screen");
}

void the_games_line_breaks_are_kept() {
  using x2::input::menu_text_lines;
  check(menu_text_lines("~03One\n\n  two ~~\n") ==
            std::vector<std::string>{"One", "two"},
        "each line read as a player sees it, empty lines dropped");
  check(menu_text_lines("").empty(), "no text is no lines");
}

/* CMenuWorldMap as GET /menu read it in sanctuary1 with acts 1 and 2
   unlocked: act 1 open, its first point focused, the second unlocked when
   `two_points`. */
MenuSnapshot worldmap_menu(bool two_points, int focused_point) {
  MenuSnapshot menu;
  menu.address = 0x27128964u;
  menu.name = "worldmap";
  menu.menu_class = "CMenuWorldMap";
  menu.items.push_back(
      item(5, "desctext1", "~05$MENU_BACK Back", 29, 21, 108, 35));
  menu.items.push_back(
      item(6, "desctext2", "~05$MENU_BACK back", 122, 21, 201, 35));
  menu.items.push_back(
      item(8, "desctext4", "~05$MENU_ACCEPT go", 308, 21, 387, 35));
  menu.items.push_back(item(12, "title", "World Map", 29, 351, 158, 365));
  const char *acts[] = {"act 1", "act 2", "act 3", "act 4", "act 5"};
  for (unsigned i = 0; i < 5u; ++i) {
    const std::string name = "option0" + std::to_string(i + 1u) + "_text";
    MenuItem act = item(26u + i, "", acts[i], 29 + 93 * static_cast<int>(i),
                        313, 108 + 93 * static_cast<int>(i), 327);
    act.name = name;
    act.flags = i < 2u ? x2::menu::kItemEnabled : 0u;
    if (i == 0u) {
      act.flags |= x2::menu::kItemFocusLit;
    }
    menu.items.push_back(act);
  }
  menu.items.push_back(item(31, "map_title", "Genosha", 29, 280, 108, 294));
  menu.items.push_back(item(32, "extract_description",
                            "A bunker used by Magneto, this plateau now lies "
                            "in\nruins",
                            29, 238, 276, 281));
  const char *points[] = {"Sanctuary", "Dead Zone", "Barren Cliffs"};
  for (unsigned i = 0; i < 3u; ++i) {
    MenuItem point =
        item(62u + i, "", points[i], 351, 273 - 36 * static_cast<int>(i), 480,
             287 - 36 * static_cast<int>(i));
    point.name = "list01_0" + std::to_string(i + 1u);
    point.flags =
        i == 0u || (two_points && i == 1u) ? x2::menu::kItemEnabled : 0u;
    menu.items.push_back(point);
  }
  const int first_point = static_cast<int>(menu.items.size()) - 3;
  menu.rows = {first_point};
  menu.focused = first_point + focused_point;
  return menu;
}

void the_world_map_is_its_acts_and_points() {
  const RetailScenePlane plane = plane_1280x720();
  const MenuSnapshot menu = worldmap_menu(true, 0);
  const auto view = x2::input::build_touch_menu_view(menu, plane);
  check(view.has_value() && view->title == "World Map",
        "the world map is replaced, titled");
  if (!view) {
    return;
  }
  check(view->tabs.size() == 2u && view->tabs[0].label == "act 1" &&
            view->tabs[0].lit && !view->tabs[1].lit,
        "the unlocked acts are tabs with the open one lit");
  const auto scene = plane.to_scene(view->tabs[1].click);
  check(scene.x >= 122.0F && scene.x < 201.0F && scene.z > 300.0F &&
            scene.z < 327.0F,
        "an act is clicked inside the box CMenuWorldMap::onMouse tests");
  check(view->rows.size() == 2u && view->rows[0].label == "Sanctuary" &&
            view->rows[1].label == "Dead Zone" && view->focused_row == 0,
        "the unlocked points are rows with the game's focus");
  check(!view->rows[0].clicks && view->rows[0].press_on_arrival &&
            !view->rows[1].press_on_arrival && !view->focus_wraps,
        "the focused point is pressed with A, another only walked to");
  check(view->facts.size() == 1u && view->facts[0].value == "Genosha" &&
            view->detail ==
                std::vector<std::string>{
                    "A bunker used by Magneto, this plateau now "
                    "lies in",
                    "ruins"},
        "the region and the focused point's description");
  check(view->footers.size() == 2u && view->footers[0].label == "Back" &&
            view->footers[1].label == "go",
        "one Back footer for the game's two, and go");

  TouchMenu touch;
  touch.set_viewport(viewport_1280x720());
  touch.set_view(view, 0u);
  auto out = tap(touch, find(touch.layout(), TouchMenuPart::row, 1)->rect, 10u);
  check(out.size() == 1u && out[0].kind == TouchMenuDelivery::Kind::pad &&
            out[0].button == TouchAction::MenuDown,
        "a tap on another point walks down to it");
  out = touch.set_view(
      x2::input::build_touch_menu_view(worldmap_menu(true, 1), plane), 20u);
  check(out.empty(), "and only selects it on arrival");
  out = tap(touch, find(touch.layout(), TouchMenuPart::row, 1)->rect, 30u);
  check(out.size() == 1u && out[0].kind == TouchMenuDelivery::Kind::pad &&
            out[0].button == TouchAction::MenuA,
        "a tap on the focused point is A, which travels");
  out = tap(touch, find(touch.layout(), TouchMenuPart::tab, 1)->rect, 40u);
  check(out.size() == 1u && out[0].kind == TouchMenuDelivery::Kind::click &&
            out[0].at.x == view->tabs[1].click.x,
        "a tap on an act is a click on it");

  MenuSnapshot locked = worldmap_menu(false, 0);
  locked.items.back().flags = 0u;
  locked.items[locked.items.size() - 3u].flags = 0u;
  check(!x2::input::build_touch_menu_view(locked, plane),
        "a world map with no unlocked point keeps the retail screen");
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
      for (const int index : menu.rows) {
        const auto &rect = menu.items[static_cast<std::size_t>(index)].rect;
        const auto scene =
            plane->to_scene(x2::input::menu_item_centre(rect, *plane));
        check(scene.x >= static_cast<float>(rect.left) &&
                  scene.x < static_cast<float>(rect.right) &&
                  scene.z >= static_cast<float>(rect.top) &&
                  scene.z < static_cast<float>(rect.bottom),
              "row " + std::to_string(index) +
                  " click is inside its hit box at " + std::to_string(width) +
                  "x" + std::to_string(height));
      }
    }
  }
  check(!RetailScenePlane::from_viewport(kAspect, 0.0F, 1.0F, 1280u, 720u),
        "a viewport with no width is no plane");
  check(!RetailScenePlane::from_viewport(kAspect, 1.0F, 1.0F, 0u, 720u),
        "a backbuffer with no width is no plane");
}

void the_layout_is_finger_sized_and_inside_the_safe_area() {
  auto view = options_view(options_menu(), plane_1280x720());
  for (int i = 0; i < 9; ++i) {
    view.rows.push_back(view.rows[1]);
    view.rows.back().slot = 100u + static_cast<unsigned>(i);
  }
  x2::presentation::LayoutViewport viewport = viewport_1280x720();
  viewport.safe_left = 40.0F;
  viewport.safe_bottom = 30.0F;
  const TouchMenuLayout layout =
      x2::input::layout_touch_menu(view, viewport, 0.0F, 0.0F);
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
      x2::input::layout_touch_menu(view, viewport, 1.0e6F, 0.0F);
  check(end.scroll == end.max_scroll, "scroll clamps to the last row");
  const TouchMenuButton *footer = find(layout, TouchMenuPart::footer, 0);
  check(footer && layout.hit(0.5F * (footer->rect.left + footer->rect.right),
                             0.5F * (footer->rect.top + footer->rect.bottom)) ==
                      static_cast<std::size_t>(footer - layout.buttons.data()),
        "a footer button is hit at its centre");
}

void a_menu_without_a_title_has_no_header_band() {
  auto view = options_view(options_menu(), plane_1280x720());
  const TouchMenuLayout titled =
      x2::input::layout_touch_menu(view, viewport_1280x720(), 0.0F, 0.0F);
  view.title.clear();
  const TouchMenuLayout untitled =
      x2::input::layout_touch_menu(view, viewport_1280x720(), 0.0F, 0.0F);
  check(untitled.title.bottom == untitled.title.top &&
            untitled.list.top == untitled.title.top &&
            untitled.list.top < titled.list.top,
        "a menu without a title starts its rows where the header band was");
}

void a_menu_without_footers_gives_their_band_to_the_list() {
  auto view = options_view(options_menu(), plane_1280x720());
  const TouchMenuLayout with =
      x2::input::layout_touch_menu(view, viewport_1280x720(), 0.0F, 0.0F);
  view.footers.clear();
  const TouchMenuLayout without =
      x2::input::layout_touch_menu(view, viewport_1280x720(), 0.0F, 0.0F);
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
  menu.set_view(options_view(snapshot, plane), 0u);
  check(menu.shown(), "the touch menu is shown");
  const TouchMenuView view = options_view(snapshot, plane);

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
  out = menu.set_view(options_view(snapshot, plane), 50u);
  check(out.empty(), "no second press until the game's focus moved");
  out = menu.set_view(options_view(snapshot, plane), 700u);
  check(out.size() == 1u && out[0].button == TouchAction::MenuUp,
        "a press the game did not answer is sent again");
  snapshot.focused = 2;
  out = menu.set_view(options_view(snapshot, plane), 710u);
  check(out.size() == 1u && out[0].button == TouchAction::MenuA,
        "and A once the focus is on the row");
  out = menu.set_view(options_view(snapshot, plane), 720u);
  check(out.empty(), "the walk is over");

  /* From row 2, row 0 is one Down through the wrap. */
  out = tap(menu, find(menu.layout(), TouchMenuPart::step_left, 0)->rect, 800u);
  check(out.size() == 1u && out[0].button == TouchAction::MenuDown,
        "a step on another row walks there first");
  snapshot.focused = 0;
  out = menu.set_view(options_view(snapshot, plane), 810u);
  check(out.size() == 1u && out[0].button == TouchAction::MenuLeft,
        "then steps it with Left");

  menu.set_view(std::nullopt, 820u);
  check(!menu.shown(), "no menu to replace hides it");
  out = tap(menu, x2::presentation::Rect{600.0F, 300.0F, 620.0F, 320.0F}, 830u);
  check(out.empty(), "and a hidden touch menu delivers nothing");
}

void a_drag_scrolls_and_does_not_press() {
  auto view = options_view(options_menu(), plane_1280x720());
  for (int i = 0; i < 9; ++i) {
    view.rows.push_back(view.rows[1]);
    view.rows.back().slot = 100u + static_cast<unsigned>(i);
  }
  TouchMenu menu;
  menu.set_viewport(viewport_1280x720());
  menu.set_view(view, 0u);
  const x2::presentation::Rect row =
      find(menu.layout(), TouchMenuPart::row, 3)->rect;
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

/* The world map's acts over enough points to scroll. */
TouchMenuView long_worldmap_view() {
  auto view = *x2::input::build_touch_menu_view(worldmap_menu(true, 0),
                                                plane_1280x720());
  for (int i = 0; i < 20; ++i) {
    view.rows.push_back(view.rows[1]);
    view.rows.back().slot = 100u + static_cast<unsigned>(i);
  }
  return view;
}

void the_tab_bar_is_pinned_above_the_list() {
  TouchMenu touch;
  touch.set_viewport(viewport_1280x720());
  touch.set_view(long_worldmap_view(), 0u);
  const TouchMenuLayout &layout = touch.layout();
  const auto *tab = find(layout, TouchMenuPart::tab, 0);
  check(tab != nullptr && tab->rect.bottom <= layout.list.top &&
            layout.detail.top >= layout.list.bottom &&
            layout.detail.bottom <= layout.footer.top &&
            tab->rect.bottom - tab->rect.top >= 48.0F,
        "the tab bar sits above the list and the detail below it");
  if (tab == nullptr) {
    return;
  }
  const float tab_top = tab->rect.top;
  const float x = 0.5F * (layout.list.left + layout.list.right);
  const float y = 0.5F * (layout.list.top + layout.list.bottom);
  touch.contact(3, {x, y}, lucent::touch::Phase::began, 10u);
  touch.contact(3, {x, y - 5000.0F}, lucent::touch::Phase::moved, 10u);
  touch.contact(3, {x, y - 5000.0F}, lucent::touch::Phase::ended, 10u);
  const auto *pinned = find(touch.layout(), TouchMenuPart::tab, 0);
  check(touch.layout().scroll > 0.0F && pinned != nullptr &&
            pinned->rect.top == tab_top,
        "the tab bar does not scroll with the list");
}

void a_long_detail_scrolls_on_its_own() {
  const TouchMenuView view = long_worldmap_view();
  TouchMenuView longer = view;
  for (int i = 0; i < 14; ++i) {
    longer.detail.push_back("line " + std::to_string(i));
  }
  TouchMenu reader;
  reader.set_viewport(viewport_1280x720());
  reader.set_view(longer, 0u);
  const TouchMenuLayout &before = reader.layout();
  check(before.detail_max_scroll > 0.0F && before.detail_scroll == 0.0F,
        "a description longer than the detail band scrolls on its own");
  const float text_top = before.detail_text_top;
  const float list_scroll = before.scroll;
  const float x = 0.5F * (before.detail.left + before.detail.right);
  const float y = before.detail.bottom - 10.0F;
  reader.contact(3, {x, y}, lucent::touch::Phase::began, 10u);
  reader.contact(3, {x, y - 60.0F}, lucent::touch::Phase::moved, 10u);
  const auto out =
      reader.contact(3, {x, y - 60.0F}, lucent::touch::Phase::ended, 10u);
  check(out.empty() && reader.layout().detail_text_top == text_top - 60.0F &&
            reader.layout().scroll == list_scroll,
        "a drag on it scrolls its lines, not the list, and presses nothing");
  reader.set_view(view, 20u);
  check(reader.layout().detail_scroll == 0.0F,
        "another description starts at its top");
}

void a_view_without_rows_reads_its_text() {
  auto view = options_view(options_menu(), plane_1280x720());
  view.rows.clear();
  view.focused_row = -1;
  for (int i = 0; i < 40; ++i) {
    view.detail.push_back("line " + std::to_string(i));
  }
  TouchMenu touch;
  touch.set_viewport(viewport_1280x720());
  touch.set_view(view, 0u);
  const TouchMenuLayout &layout = touch.layout();
  check(layout.reading && layout.detail.top == layout.list.top &&
            layout.detail.bottom == layout.list.bottom &&
            layout.max_scroll > 0.0F,
        "the text takes the list's place and scrolls when it is longer");
  const float top = layout.detail_text_top;
  const float x = 0.5F * (layout.list.left + layout.list.right);
  const float y = layout.list.bottom - 10.0F;
  touch.contact(4, {x, y}, lucent::touch::Phase::began, 10u);
  touch.contact(4, {x, y - 200.0F}, lucent::touch::Phase::moved, 10u);
  const auto out =
      touch.contact(4, {x, y - 200.0F}, lucent::touch::Phase::ended, 10u);
  check(out.empty() && touch.layout().detail_text_top == top - 200.0F,
        "a drag scrolls the text and presses nothing");
}

} // namespace

int main() {
  retail_text_reads_as_a_player_sees_it();
  main_options_and_the_pda_are_native();
  a_delivered_click_lands_in_the_hit_box();
  the_layout_is_finger_sized_and_inside_the_safe_area();
  a_menu_without_footers_gives_their_band_to_the_list();
  a_menu_without_a_title_has_no_header_band();
  a_tap_delivers_the_games_own_input();
  the_tab_bar_is_pinned_above_the_list();
  a_long_detail_scrolls_on_its_own();
  a_view_without_rows_reads_its_text();
  the_games_line_breaks_are_kept();
  the_world_map_is_its_acts_and_points();
  a_drag_scrolls_and_does_not_press();
  return report("touch_menu");
}
