/*
 * The touch menu's pure parts through the shipping code: the view built from a
 * retail menu snapshot, its layout and hit test, what a tap delivers to the
 * game, and the scene-plane mapping a delivered click crosses.
 */
#include "../src/input/touch_menu.hpp"
#include "../src/input/touch_menu_parts.hpp"

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
  check(touch_menu_text("Recovers 33% of max $HP with a chance") ==
            "Recovers 33% of max HP with a chance",
        "a stat token draws as its name, as the retail shop shows it");
  check(touch_menu_text("~02Limit:~~ 10") == "Limit: 10",
        "the style reset escape is removed");
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

const MenuItem *find_item(const MenuSnapshot &menu, const std::string &name) {
  for (const MenuItem &candidate : menu.items) {
    if (candidate.name == name) {
      return &candidate;
    }
  }
  return nullptr;
}

/* CMenuShop's training tab as GET /menu read it in the jungle: the three
   tabs with training lit, the list box holding `entries` entries. */
MenuSnapshot shop_menu(int entries, int top, int selected) {
  MenuSnapshot menu;
  menu.address = 0x27128964u;
  menu.name = "shop";
  menu.menu_class = "CMenuShop";
  menu.mode = 0u;
  menu.items.push_back(
      item(1, "desctext2", "~05$MENU_ACCEPT Buy", 82, 21, 161, 35));
  menu.items.push_back(
      item(2, "desctext3", "~05$MENU_OK Accept", 215, 21, 294, 35));
  menu.items.push_back(item(14, "shop_option01", "buy", 229, 354, 288, 368));
  menu.items.push_back(item(15, "shop_option02", "sell", 319, 354, 378, 368));
  MenuItem training = item(16, "shop_option03", "training", 409, 354, 468, 368);
  training.flags |= x2::menu::kItemFocusLit;
  menu.items.push_back(training);
  MenuItem cost = item(31, "item_cost_value", "~0620000", 419, 53, 482, 67);
  menu.items.push_back(item(30, "label_cost", "cost", 370, 54, 403, 68));
  menu.items.push_back(item(24, "money_value", "2000", 50, 58, 108, 72));
  menu.items.push_back(
      item(26, "label_inventory_count", "gear", 360, 119, 403, 133));
  menu.items.push_back(item(27, "inventory_count", "0/20", 419, 118, 482, 132));
  menu.items.push_back(
      item(28, "label_owner", "~02Limit:~~ 10", 216, 53, 290, 67));
  menu.items.push_back(item(29, "item_desc",
                            "Recovers 33% of max $HP with a chance for 66% "
                            "based on\nBody\n",
                            215, 75, 482, 123));
  menu.items.push_back(cost);
  MenuItem list = item(46, "list", "", 215, 191, 482, 377);
  x2::menu::ListBoxState box;
  for (int i = 0; i < entries; ++i) {
    box.entries.push_back("Entry " + std::to_string(i));
  }
  box.top = top;
  box.selected = selected;
  box.visible_rows = 23;
  box.row_height = 8;
  box.hit = {215, 143, 482, 329};
  list.list_box = box;
  list.focused = true;
  menu.items.push_back(list);
  menu.rows = {static_cast<int>(menu.items.size()) - 1};
  menu.focused = menu.rows[0];
  return menu;
}

void the_shop_is_its_tabs_and_entries() {
  const RetailScenePlane plane = plane_1280x720();
  const MenuSnapshot menu = shop_menu(30, 4, 6);
  const auto view = x2::input::build_touch_menu_view(menu, plane);
  check(view.has_value(), "the shop is replaced");
  if (!view) {
    return;
  }
  check(view->tabs.size() == 3u && view->tabs[0].label == "buy" &&
            view->tabs[2].label == "training" && view->tabs[2].lit &&
            !view->tabs[0].lit,
        "the tabs are a bar of their own with the open one lit");
  check(view->rows.size() == 30u && view->rows[0].label == "Entry 0",
        "the rows are the list's entries only");
  check(view->focused_row == 6 && view->rows[6].focused,
        "the list's selection is the menu's focus");
  check(!view->focus_wraps, "the list's Up/Down does not wrap");
  bool tabs_inside = true;
  for (std::size_t i = 0; i < 3u; ++i) {
    const auto &rect =
        find_item(menu, "shop_option0" + std::to_string(i + 1))->rect;
    const auto scene = plane.to_scene(view->tabs[i].click);
    tabs_inside = tabs_inside && scene.x >= static_cast<float>(rect.left) &&
                  scene.x < static_cast<float>(rect.right) &&
                  scene.z >= static_cast<float>(rect.top) + 3.0F &&
                  scene.z < static_cast<float>(rect.bottom) - 3.0F;
  }
  check(tabs_inside, "a tab is clicked inside the box CMenuShop tests");
  bool rows_land = true;
  for (int entry = 4; entry < 27; ++entry) {
    const auto &row = view->rows[static_cast<std::size_t>(entry)];
    const auto scene = plane.to_scene(row.click);
    /* CMenuItemListBox::onMouse (0x005c0e10): (top edge - y) / row height. */
    const int window_row = (329 - static_cast<int>(std::floor(scene.z))) / 8;
    rows_land = rows_land && row.clicks && !row.press_on_arrival &&
                window_row == entry - 4 && scene.x >= 215.0F &&
                scene.x < 482.0F;
  }
  check(rows_land, "each entry in the window is clicked on its own row");
  check(!view->rows[3].clicks && !view->rows[27].clicks,
        "an entry outside the window has no row to click");
  check(view->rows[6].value == "20000" && view->rows[5].value.empty(),
        "the selected entry shows the cost the game priced it at");
  check(view->facts.size() == 4u && view->facts[0].label == "cost" &&
            view->facts[0].value == "20000" && view->facts[0].warn &&
            view->facts[1].label == "money" && view->facts[1].value == "2000" &&
            !view->facts[1].warn && view->facts[2].label == "gear" &&
            view->facts[2].value == "0/20" && view->facts[3].label.empty() &&
            view->facts[3].value == "Limit: 10",
        "cost, money, gear and limit, as the game's own items show them");
  check(
      view->detail ==
          std::vector<std::string>{
              "Recovers 33% of max HP with a chance for 66% based on", "Body"},
      "the selected entry's description, in the game's own lines");
  check(view->footers.size() == 2u && view->footers[0].label == "Buy",
        "the shop's Buy and Accept footers");

  MenuSnapshot stash = shop_menu(3, 0, 0);
  stash.mode = 1u;
  check(!x2::input::build_touch_menu_view(stash, plane),
        "the stash keeps the retail screen");
  MenuSnapshot unread = shop_menu(3, 0, 0);
  unread.mode.reset();
  check(!x2::input::build_touch_menu_view(unread, plane),
        "a shop whose mode was not read keeps the retail screen");

  TouchMenu touch;
  touch.set_viewport(viewport_1280x720());
  MenuSnapshot live = shop_menu(30, 4, 6);
  touch.set_view(x2::input::build_touch_menu_view(live, plane), 0u);
  const auto visible = *x2::input::build_touch_menu_view(live, plane);
  const TouchMenuLayout &layout = touch.layout();
  const auto *tab = find(layout, TouchMenuPart::tab, 0);
  const auto *first_row = find(layout, TouchMenuPart::row, 0);
  check(tab != nullptr && first_row != nullptr &&
            tab->rect.bottom <= layout.list.top &&
            layout.detail.top >= layout.list.bottom &&
            layout.detail.bottom <= layout.footer.top &&
            tab->rect.bottom - tab->rect.top >= 48.0F,
        "the tab bar sits above the list and the detail below it");
  auto out = tap(touch, tab->rect, 10u);
  check(out.size() == 1u && out[0].kind == TouchMenuDelivery::Kind::click &&
            out[0].at.x == visible.tabs[0].click.x &&
            out[0].at.y == visible.tabs[0].click.y,
        "a tap on a tab is a click on the tab");
  const X2Rect entry_row = find(touch.layout(), TouchMenuPart::row, 6)->rect;
  out = tap(touch, entry_row, 20u);
  check(out.size() == 1u && out[0].kind == TouchMenuDelivery::Kind::click &&
            out[0].at.y == visible.rows[6].click.y,
        "a tap on a shown entry is a click on its row");

  /* Entry 28 is below the window: walk the selection down to it, then stop. */
  const float x = 0.5F * (entry_row.left + entry_row.right);
  touch.contact(3, {x, entry_row.top}, lucent::touch::Phase::began, 25u);
  touch.contact(3, {x, entry_row.top - 5000.0F}, lucent::touch::Phase::moved,
                25u);
  touch.contact(3, {x, entry_row.top - 5000.0F}, lucent::touch::Phase::ended,
                25u);
  const auto *pinned = find(touch.layout(), TouchMenuPart::tab, 0);
  check(pinned != nullptr && pinned->rect.top == tab->rect.top,
        "the tab bar does not scroll with the list");
  const auto *far = find(touch.layout(), TouchMenuPart::row, 28);
  check(far != nullptr, "the far entry has a button");
  if (far == nullptr) {
    return;
  }
  out = tap(touch, far->rect, 30u);
  check(out.size() == 1u && out[0].kind == TouchMenuDelivery::Kind::pad &&
            out[0].button == TouchAction::MenuDown,
        "an entry outside the window is walked to with Down, not the wrap");
  live.items.back().list_box->selected = 28;
  live.items.back().list_box->top = 6;
  out = touch.set_view(x2::input::build_touch_menu_view(live, plane), 40u);
  check(out.empty(), "and only selected on arrival, as a click would");
}

/* CMenuCodex as GET /menu read it from gameplay: fifteen heroes, the list
   (mode 0) or the loaded entry's name and description (mode 1). */
MenuSnapshot codex_menu(std::uint32_t mode, int selected) {
  MenuSnapshot menu;
  menu.address = 0x27129014u;
  menu.name = "codex";
  menu.menu_class = "CMenuCodex";
  menu.mode = mode;
  menu.items.push_back(item(1, "title", "Codex", 29, 404, 298, 418));
  menu.items.push_back(
      item(2, "desctext1", "~05$MENU_BACK Back", 29, 21, 108, 35));
  menu.items.push_back(
      item(3, "desctext3", "~05$MENU_DETAILS Details", 215, 21, 294, 35));
  MenuItem name = item(4, "name", "~03Wolverine", 29, 404, 298, 418);
  MenuItem desc = item(5, "desc",
                       "A loner and a man without a memory, Wolverine was\n"
                       "discovered by James MacDonald Hudson\n\n",
                       29, 122, 298, 391);
  MenuItem list = item(6, "list", "", 29, 122, 298, 391);
  x2::menu::ListBoxState box;
  for (int i = 0; i < 15; ++i) {
    box.entries.push_back("Hero " + std::to_string(i));
  }
  box.selected = selected;
  box.visible_rows = 11;
  box.row_height = 24;
  box.hit = {29, 122, 298, 391};
  list.list_box = box;
  if (mode == 0u) {
    name.flags |= x2::menu::kItemHidden;
    desc.flags |= x2::menu::kItemHidden;
  } else {
    list.flags |= x2::menu::kItemHidden;
  }
  menu.items.push_back(name);
  menu.items.push_back(desc);
  menu.items.push_back(list);
  menu.rows = {static_cast<int>(menu.items.size()) - 1};
  menu.focused = menu.rows[0];
  return menu;
}

void the_codex_lists_its_heroes_and_reads_one() {
  const RetailScenePlane plane = plane_1280x720();
  const MenuSnapshot list = codex_menu(0u, 3);
  const auto view = x2::input::build_touch_menu_view(list, plane);
  check(view.has_value() && view->rows.size() == 15u &&
            view->title == "Codex" && view->focused_row == 3,
        "the codex list is its heroes, titled, with the selection focused");
  if (!view) {
    return;
  }
  bool accepts = true;
  for (const auto &row : view->rows) {
    accepts = accepts && !row.clicks && row.press_on_arrival;
  }
  check(accepts, "a codex entry is walked to and accepted, which loads it");
  check(view->footers.size() == 2u && view->footers[1].label == "Details",
        "the codex's Back and Details footers");

  TouchMenu touch;
  touch.set_viewport(viewport_1280x720());
  touch.set_view(view, 0u);
  auto out = tap(touch, find(touch.layout(), TouchMenuPart::row, 5)->rect, 10u);
  check(out.size() == 1u && out[0].kind == TouchMenuDelivery::Kind::pad &&
            out[0].button == TouchAction::MenuDown,
        "a tap below the selection walks down to it");
  const MenuSnapshot arrived = codex_menu(0u, 5);
  out = touch.set_view(x2::input::build_touch_menu_view(arrived, plane), 20u);
  check(out.size() == 1u && out[0].kind == TouchMenuDelivery::Kind::pad &&
            out[0].button == TouchAction::MenuA,
        "and accepts it on arrival");

  const MenuSnapshot reading = codex_menu(1u, 3);
  const auto read = x2::input::build_touch_menu_view(reading, plane);
  check(read.has_value() && read->rows.empty() && read->title == "Wolverine" &&
            read->detail ==
                std::vector<std::string>{
                    "A loner and a man without a memory, Wolverine was",
                    "discovered by James MacDonald Hudson"},
        "Details reads the loaded entry's name and its lines");
  if (!read) {
    return;
  }
  TouchMenuView longer = *read;
  for (int i = 0; i < 30; ++i) {
    longer.detail.push_back("line " + std::to_string(i));
  }
  touch.set_view(longer, 30u);
  const TouchMenuLayout &layout = touch.layout();
  check(layout.reading && layout.detail.top == layout.list.top &&
            layout.detail.bottom == layout.list.bottom &&
            layout.max_scroll > 0.0F,
        "the text takes the list's place and scrolls when it is longer");
  const float top = layout.detail_text_top;
  const float x = 0.5F * (layout.list.left + layout.list.right);
  const float y = layout.list.bottom - 10.0F;
  touch.contact(4, {x, y}, lucent::touch::Phase::began, 40u);
  touch.contact(4, {x, y - 200.0F}, lucent::touch::Phase::moved, 40u);
  out = touch.contact(4, {x, y - 200.0F}, lucent::touch::Phase::ended, 40u);
  check(out.empty() && touch.layout().detail_text_top == top - 200.0F,
        "a drag scrolls the text and presses nothing");

  const MenuSnapshot other = codex_menu(2u, 3);
  check(!x2::input::build_touch_menu_view(other, plane),
        "a codex mode not read keeps the retail screen");
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

void a_menu_without_a_title_has_no_header_band() {
  auto view =
      *x2::input::build_touch_menu_view(options_menu(), plane_1280x720());
  const TouchMenuLayout titled =
      x2::input::layout_touch_menu(view, viewport_1280x720(), 0.0F);
  view.title.clear();
  const TouchMenuLayout untitled =
      x2::input::layout_touch_menu(view, viewport_1280x720(), 0.0F);
  check(untitled.title.bottom == untitled.title.top &&
            untitled.list.top == untitled.title.top &&
            untitled.list.top < titled.list.top,
        "a menu without a title starts its rows where the header band was");
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
  a_menu_without_a_title_has_no_header_band();
  a_tap_delivers_the_games_own_input();
  the_team_party_is_its_heroes();
  the_shop_is_its_tabs_and_entries();
  the_codex_lists_its_heroes_and_reads_one();
  the_games_line_breaks_are_kept();
  the_world_map_is_its_acts_and_points();
  a_drag_scrolls_and_does_not_press();
  if (failures) {
    std::printf("touch_menu: %d of %d check(s) failed\n", failures, checks);
    return 1;
  }
  std::printf("touch_menu: %d check(s) passed\n", checks);
  return 0;
}
