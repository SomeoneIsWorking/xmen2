/*
 * CMenuTeam's touch views through the shipping builder: the party's hero
 * summaries and the roster's carousel as rows, and what a tap on each asks of
 * the game.
 */
#include "touch_menu_fixture.hpp"

#include "../src/input/touch_menu_parts.hpp"

#include <string>
#include <vector>

namespace {

using namespace x2::test::touch_menu;
using x2::native::HeroRecord;

HeroRecord hero(const char *name, const char *display, int level,
                bool fallen = false, bool unlocked = true) {
  HeroRecord out;
  out.name = name;
  out.display_name = display;
  out.level = level;
  out.fallen = fallen;
  out.unlocked = unlocked;
  return out;
}

/* CMenuTeam as GET /menu read it in the jungle after `loadmap ... 0 1`: the
   party Magneto, Cyclops, an empty slot and Storm, Cyclops selected. */
MenuSnapshot team_menu(std::uint32_t mode) {
  MenuSnapshot menu;
  menu.address = 0x27128964u;
  menu.name = "team";
  menu.menu_class = "CMenuTeam";
  menu.mode = mode;
  menu.items.push_back(item(58, "item_health", "", 15, -77, 61, -46));
  const char *labels[] = {"Magneto", "Cyclops", "", "Storm"};
  const char *display[] = {"Magneto", "Cyclops", "", "Storm"};
  for (int i = 0; i < 4; ++i) {
    const std::string name = "char_summary0" + std::to_string(i + 1);
    MenuItem summary = item(static_cast<unsigned>(52 + i), "", labels[i], 364,
                            313 - 80 * i, 479, 371 - 80 * i);
    summary.name = name;
    summary.item_class = x2::menu::ItemClass::char_summary;
    if (*labels[i] != '\0') {
      summary.hero = hero(labels[i], display[i], 3 + i);
    }
    if (i == 1) {
      summary.flags |= x2::menu::kItemFocusLit;
    }
    menu.items.push_back(summary);
  }
  menu.items.push_back(
      item(38, "desctext4", "~05$MENU_OTHER Details", 268, 21, 347, 35));
  menu.items.push_back(
      item(39, "desctext5", "~05$MENU_OK Accept", 401, 21, 480, 35));
  menu.rows = {0};
  menu.focused = 0;
  return menu;
}

/* The roster as GET /menu read it after Replace on the empty slot, Wolverine
   killed: the carousel's store in its own order, the locked heroes last, and
   the first card at entry `first`. */
MenuSnapshot roster_menu(int first) {
  MenuSnapshot menu = team_menu(1u);
  menu.items.resize(5);
  menu.items.push_back(
      item(36, "desctext3", "~05$MENU_ACCEPT Replace", 135, 21, 214, 35));
  menu.items.push_back(
      item(38, "desctext4", "~05$MENU_OTHER Details", 268, 21, 347, 35));
  menu.items.push_back(
      item(39, "desctext5", "~05$MENU_OK Accept", 401, 21, 480, 35));
  MenuItem list = item(72, "roster_portrait01", "", 41, 599, 98, 658);
  list.item_class = x2::menu::ItemClass::list_chars;
  list.flags |= x2::menu::kItemFocusLit;
  x2::menu::ListBoxState box;
  const std::vector<HeroRecord> store = {
      hero("Bishop", "Bishop", 2),
      hero("Phoenix", "Jean Grey", 4),
      hero("Wolverine", "Wolverine", 7, true),
      hero("sabretooth_hero", "Sabretooth", 1),
      hero("Deadpool", "DeadPool", 1, false, false),
      hero("Professorx", "Professor X", 1, false, false)};
  for (const HeroRecord &entry : store) {
    box.entries.push_back(entry.name);
    box.values.emplace_back();
    list.heroes.emplace_back(entry);
  }
  box.selected = first;
  list.list_box = box;
  menu.items.push_back(list);
  MenuItem middle = item(76, "roster_summary02", "", 109, 224, 224, 282);
  middle.item_class = x2::menu::ItemClass::char_summary;
  middle.masks_locked = true;
  menu.items.push_back(middle);
  return menu;
}

void the_party_is_its_heroes() {
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
  check(view->rows[2].label == "Empty slot" && view->rows[2].value.empty(),
        "the empty party slot is named as one");
  check(view->rows[1].value == "Level 4", "a party hero shows its level");
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

void the_roster_is_its_named_heroes() {
  const RetailScenePlane plane = plane_1280x720();
  const auto view = x2::input::build_touch_menu_view(roster_menu(0), plane);
  check(view.has_value(), "the roster is replaced");
  if (!view) {
    return;
  }
  check(view->rows.size() == 4u && view->rows[0].label == "Bishop" &&
            view->rows[1].label == "Jean Grey" &&
            view->rows[3].label == "Sabretooth",
        "the roster's heroes by the names the cards draw, locked ones left "
        "out");
  check(view->rows[2].value == "Fallen, level 7" &&
            view->rows[0].value == "Level 2",
        "a roster hero shows its level, and a fallen one says so");
  check(view->focused_row == 1 && view->rows[1].entry == 1,
        "the middle card, one past the list's +0xac, is the focused hero");
  check(!view->rows[0].clicks && view->rows[0].press_on_arrival,
        "a roster hero is walked to with the pad and chosen with A");
  check(view->footers.size() == 3u && view->footers[0].label == "Replace",
        "the roster's Replace, Details and Accept footers");

  const auto wrapped = x2::input::build_touch_menu_view(roster_menu(5), plane);
  check(wrapped && wrapped->focused_row == 0,
        "the middle card wraps to the first entry");
  const auto on_locked =
      x2::input::build_touch_menu_view(roster_menu(3), plane);
  check(on_locked && on_locked->focused_row == -1,
        "a locked hero on the middle card focuses no row");
  MenuSnapshot unmasked = roster_menu(0);
  unmasked.items.back().masks_locked = false;
  const auto all = x2::input::build_touch_menu_view(unmasked, plane);
  check(all && all->rows.size() == 6u && all->rows[5].label == "Professor X",
        "cards that do not mask locked heroes name them all");

  TouchMenu touch;
  touch.set_viewport(viewport_1280x720());
  touch.set_view(view, 0u);
  const auto first =
      tap(touch, find(touch.layout(), TouchMenuPart::row, 2)->rect, 10u);
  check(first.size() == 1u && first[0].kind == TouchMenuDelivery::Kind::pad &&
            first[0].button == TouchAction::MenuDown,
        "a tap on Wolverine turns the carousel down towards it");
  const auto arrived = touch.set_view(
      x2::input::build_touch_menu_view(roster_menu(1), plane), 20u);
  check(arrived.size() == 1u && arrived[0].button == TouchAction::MenuA,
        "on arrival A chooses or revives Wolverine");

  /* Bishop is the middle card; Sabretooth is three entries down and three up
     through the two locked heroes, though only one row up. */
  TouchMenu around;
  around.set_viewport(viewport_1280x720());
  around.set_view(wrapped, 0u);
  const auto toward =
      tap(around, find(around.layout(), TouchMenuPart::row, 3)->rect, 10u);
  check(toward.size() == 1u && toward[0].button == TouchAction::MenuDown,
        "a walk counts the carousel's locked entries, not the rows");
}

} // namespace

int main() {
  the_party_is_its_heroes();
  the_roster_is_its_named_heroes();
  return report("touch_menu_team");
}
