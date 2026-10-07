/*
 * CMenuTeam's touch views through the shipping builder: the party's hero
 * summaries, the roster's carousel and a hero's stats, skills, gear and ai
 * tabs as rows, and what a tap on each asks of the game.
 */
#include "touch_menu_fixture.hpp"

#include "../src/input/touch_menu_parts.hpp"
#include "../src/input/touch_menu_team_details.hpp"
#include "../src/native/extraction_revive_policy.hpp"

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
    box.columns.emplace_back();
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
        "a detail mode without its tab bar keeps the retail screen");
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
  check(view->rows[2].value == "Fallen, level 7, revive 200" &&
            view->rows[0].value == "Level 2",
        "a roster hero shows its level, and a fallen one says so with its "
        "revive cost");
  MenuSnapshot veteran = roster_menu(0);
  veteran.items.back().masks_locked = true;
  for (auto &entry : veteran.items[veteran.items.size() - 2u].heroes) {
    entry->level = 40;
  }
  const auto costly = x2::input::build_touch_menu_view(veteran, plane);
  check(costly && costly->rows.size() > 2u &&
            costly->rows[2].value ==
                "Fallen, level 40, revive " +
                    std::to_string(x2::extraction::revive_cost(40)) &&
            costly->rows[2].value.ends_with("3200"),
        "a fallen hero's revive cost is the game's max(200, 2 * level^2)");
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

MenuItem lit(MenuItem item) {
  item.flags |= x2::menu::kItemFocusLit;
  return item;
}

MenuItem list_box(unsigned slot, const char *name,
                  const std::vector<std::vector<std::string>> &records,
                  int selected, bool focused) {
  MenuItem list = item(slot, name, "", 215, 112, 482, 287);
  list.item_class = x2::menu::ItemClass::list_box;
  list.focused = focused;
  x2::menu::ListBoxState box;
  for (const std::vector<std::string> &record : records) {
    box.entries.push_back(record.front());
    box.columns.emplace_back(record.begin() + 1, record.end());
  }
  box.selected = selected;
  list.list_box = box;
  return list;
}

/* A hero's details as GET /menu read them for Magneto at level 40: the tab
   bar with tab `mode` lit, the hero's name and level, and the tab's items. */
MenuSnapshot details_menu(std::uint32_t mode) {
  MenuSnapshot menu = team_menu(mode);
  menu.items.clear();
  menu.items.push_back(item(85, "name", "Magneto", 217, 328, 338, 342));
  menu.items.push_back(item(88, "level", "40", 465, 326, 480, 340));
  const char *tabs[] = {"stats", "skills", "gear", "ai"};
  for (int i = 0; i < 4; ++i) {
    const std::string name = "detail_option0" + std::to_string(i + 1) + "_text";
    MenuItem tab = item(static_cast<unsigned>(166 + i), "", tabs[i],
                        217 + 68 * i, 354, 276 + 68 * i, 368);
    tab.name = name;
    menu.items.push_back(
        i + 2 == static_cast<int>(mode == 6u ? 3u : mode) ? lit(tab) : tab);
  }
  return menu;
}

/* The footer every tab ends with, after the tab's own. */
void push_accept(MenuSnapshot *menu) {
  menu->items.push_back(
      item(39, "desctext5", "~05$MENU_BACK Accept", 401, 21, 480, 35));
}

MenuSnapshot stats_menu() {
  MenuSnapshot menu = details_menu(2u);
  const char *stats[] = {"body", "focus", "strike", "speed"};
  const char *values[] = {"26", "59", "26", "36"};
  for (int i = 0; i < 4; ++i) {
    const std::string label = std::string("label_") + stats[i];
    MenuItem row = item(static_cast<unsigned>(89 + i), "", stats[i], 217,
                        309 - 14 * i, 272, 323 - 14 * i);
    row.name = label;
    MenuItem value = item(static_cast<unsigned>(107 + i), stats[i], values[i],
                          277, 309 - 14 * i, 304, 323 - 14 * i);
    menu.items.push_back(i == 0 ? lit(row) : row);
    menu.items.push_back(i == 0 ? lit(value) : value);
  }
  menu.items.push_back(
      item(115, "label_statpoints", "remaining points", 217, 47, 338, 61));
  menu.items.push_back(item(117, "points_stats", "156", 461, 49, 476, 63));
  menu.items.push_back(item(93, "label_hp", "hp", 315, 309, 328, 323));
  menu.items.push_back(item(102, "health", "273/303", 349, 309, 432, 323));
  menu.items.push_back(item(98, "label_re", "\xc2\xb2", 437, 309, 450, 323));
  menu.items.push_back(item(111, "resist_energy", "0", 455, 309, 476, 323));
  menu.items.push_back(item(112, "resist_mental", "10", 455, 295, 476, 309));
  menu.items.push_back(
      item(118, "stat_desc",
           "Increases your health points ($HP).\n\n~02Energy Resistance~~ "
           "($RES_ENERGY)\n~02Mental Resistance~~ ($RES_MENTAL)",
           215, 74, 482, 230));
  menu.items.push_back(
      item(36, "desctext2", "~05$MENU_ACCEPT Add", 62, 21, 141, 35));
  push_accept(&menu);
  return menu;
}

MenuSnapshot ai_menu() {
  MenuSnapshot menu = details_menu(5u);
  const char *rows[] = {"heal_pickup", "heal",        "level",     "power",
                        "auto_traits", "auto_skills", "auto_equip"};
  const char *labels[] = {
      "ai heal when full", "ai heal",        "ai mode",      "ai skill",
      "ai auto-trait",     "ai auto-skills", "ai auto-equip"};
  const char *values[] = {
      "Yes", "below 40%", "aggressive", "$ATTACK (Levitation)",
      "No",  "No",        "No"};
  for (int i = 0; i < 7; ++i) {
    MenuItem label = item(static_cast<unsigned>(134 + i), "", labels[i], 217,
                          309 - 14 * i, 291, 323 - 14 * i);
    label.name = std::string("label_ai_") + rows[i];
    MenuItem value = item(static_cast<unsigned>(148 + i), "", values[i], 406,
                          309 - 14 * i, 480, 323 - 14 * i);
    value.name = std::string("ai_") + rows[i];
    menu.items.push_back(i == 2 ? lit(label) : label);
    menu.items.push_back(i == 2 ? lit(value) : value);
  }
  menu.items.push_back(item(155, "ai_desc",
                            "~02Aggressive~~\nAlways attacks your target "
                            "immediately",
                            215, 74, 482, 230));
  menu.items.push_back(
      item(37, "desctext3", "~05$MENU_ACCEPT Change", 155, 21, 234, 35));
  push_accept(&menu);
  return menu;
}

/* Rank glyphs as the model reads them: Latin-1 0xd7..0xdb in UTF-8. */
const std::string kFiller = "\xc3\x9b";
const std::string kOwned = "\xc3\x99";
const std::string kAdded = "\xc3\x9a";
const std::string kOpen = "\xc3\x98";
const std::string kLocked = "\xc3\x97";

std::string glyphs(const std::string &glyph, int count) {
  std::string out;
  for (int i = 0; i < count; ++i) {
    out += glyph;
  }
  return out;
}

MenuSnapshot skills_menu() {
  MenuSnapshot menu = details_menu(3u);
  MenuItem list = list_box(126, "skill_list",
                           {{"Levitation", "~42Special~~",
                             kOwned + glyphs(kOpen, 13) + glyphs(kLocked, 6)},
                            {"Magnetic Shell", "~42Beam~~",
                             glyphs(kFiller, 2) + kOwned + kAdded +
                                 glyphs(kOpen, 10) + glyphs(kLocked, 6)},
                            {"Shrapnel Sentry", "~42Trap~~",
                             "Req: ~03Level 14~~, ~06Magnetic Shell~~"}},
                           0, true);
  list.item_class = x2::menu::ItemClass::skills;
  menu.items.push_back(lit(list));
  menu.items.push_back(
      item(116, "label_skillpoints", "remaining points", 217, 47, 338, 61));
  menu.items.push_back(item(127, "points_skills", "37", 461, 49, 476, 63));
  menu.items.push_back(
      item(36, "desctext2", "~05$MENU_ACCEPT Add", 62, 21, 141, 35));
  push_accept(&menu);
  return menu;
}

MenuSnapshot gear_menu(bool inventory) {
  MenuSnapshot menu = details_menu(4u);
  menu.items.push_back(list_box(128, "equipment",
                                {{"Fortified Waistband"},
                                 {"--- [ Nothing Equipped ] ---"},
                                 {"--- [ Nothing Equipped ] ---"}},
                                0, !inventory));
  menu.items.push_back(list_box(
      132, "equipment_inv", {{"Coil of Swiftness"}, {"Fortified Waistband"}},
      inventory ? 0 : -1, inventory));
  menu.items.push_back(
      item(129, "equipment_desc",
           "~103~~ $DR.  +~103~~ $EP per Knockout (~03Level 20~~)", 215, 43,
           482, 100));
  menu.items.push_back(
      item(130, "inventory_count", "1/20", 146, 131, 220, 145));
  menu.items.push_back(
      item(131, "inventory_count_title", "gear", 146, 148, 220, 162));
  menu.items.push_back(
      item(35, "desctext2", "~05$MENU_SUBTRACT Unequip", 62, 21, 141, 35));
  menu.items.push_back(
      item(38, "desctext4", "~~$MENU_DROP Drop", 268, 21, 347, 35));
  push_accept(&menu);
  return menu;
}

bool has_line(const TouchMenuView &view, const std::string &line) {
  for (const std::string &shown : view.detail) {
    if (shown == line) {
      return true;
    }
  }
  return false;
}

std::vector<TouchMenuDelivery> tap_row(const std::optional<TouchMenuView> &view,
                                       int row) {
  TouchMenu touch;
  touch.set_viewport(viewport_1280x720());
  touch.set_view(view, 0u);
  const TouchMenuButton *button = find(touch.layout(), TouchMenuPart::row, row);
  if (button == nullptr) {
    return {};
  }
  return tap(touch, button->rect, 10u);
}

void the_stats_tab_walks_to_a_stat_and_adds_on_it() {
  const RetailScenePlane plane = plane_1280x720();
  const MenuSnapshot menu = stats_menu();
  const auto view = x2::input::build_touch_menu_view(menu, plane);
  check(view.has_value(), "a hero's stats tab is replaced");
  if (!view) {
    return;
  }
  check(view->title == "Magneto" && view->tabs.size() == 4u &&
            view->tabs[0].lit && view->tabs[3].label == "ai",
        "the hero's name over the game's four tabs, stats lit");
  check(view->rows.size() == 4u && view->rows[1].label == "focus" &&
            view->rows[1].value == "59",
        "one row per stat, its label and the game's value");
  check(view->focused_row == 0 && !view->rows[0].clicks &&
            view->rows[0].press_on_arrival && !view->rows[2].press_on_arrival,
        "the lit stat is the current one, and only it is pressed on a tap");
  check(view->facts.size() == 2u && view->facts[0].value == "40" &&
            view->facts[1].label == "remaining points" &&
            view->facts[1].value == "156",
        "the hero's level and the points left to spend");
  check(has_line(*view, "hp 273/303"),
        "a derived stat as its own label and value");
  check(has_line(*view, "Energy Resistance 0") &&
            has_line(*view, "Mental Resistance 10"),
        "the description's resistance icons read as their values");
  check(view->footers.size() == 2u && view->footers[0].label == "Add",
        "the game's Add and Accept footers");

  const auto walk = tap_row(view, 2);
  check(walk.size() == 1u && walk[0].kind == TouchMenuDelivery::Kind::pad &&
            walk[0].button == TouchAction::MenuDown,
        "a tap on strike walks down to it rather than clicking, which "
        "would also add a point");
  const auto add = tap_row(view, 0);
  check(add.size() == 1u && add[0].button == TouchAction::MenuA,
        "a tap on the current stat presses A, which adds a point");

  TouchMenu touch;
  touch.set_viewport(viewport_1280x720());
  touch.set_view(view, 0u);
  const TouchMenuButton *skills = find(touch.layout(), TouchMenuPart::tab, 1);
  const auto opened = skills == nullptr ? std::vector<TouchMenuDelivery>{}
                                        : tap(touch, skills->rect, 10u);
  const auto scene = opened.size() == 1u ? plane.to_scene(opened[0].at)
                                         : x2::presentation::ScenePoint{};
  check(opened.size() == 1u &&
            opened[0].kind == TouchMenuDelivery::Kind::click &&
            scene.x >= 285.0F && scene.x < 344.0F && scene.z >= 354.0F &&
            scene.z < 368.0F,
        "a tap on the skills tab clicks the game's own tab");
}

void the_ai_tab_walks_its_settings() {
  const auto view =
      x2::input::build_touch_menu_view(ai_menu(), plane_1280x720());
  check(view.has_value() && view->rows.size() == 7u,
        "the ai tab is one row per setting");
  if (!view || view->rows.size() != 7u) {
    return;
  }
  check(view->rows[3].value == "ATTACK (Levitation)" &&
            view->rows[6].label == "ai auto-equip",
        "each setting by the game's label and value");
  check(view->focused_row == 2, "the lit setting is the current one");
  check(view->detail.size() == 2u && view->detail[0] == "Aggressive",
        "the current setting's description below");
  check(view->focus_wraps, "the game's Up/Down wraps through the settings");
  const auto walk = tap_row(view, 1);
  check(walk.size() == 1u && walk[0].kind == TouchMenuDelivery::Kind::pad &&
            walk[0].button == TouchAction::MenuUp,
        "a tap on ai heal walks up to it rather than clicking, which would "
        "also change it");
  const auto change = tap_row(view, 2);
  check(change.size() == 1u && change[0].button == TouchAction::MenuA,
        "a tap on the current setting presses A, which changes it");
}

void the_skills_tab_reads_ranks() {
  const auto view =
      x2::input::build_touch_menu_view(skills_menu(), plane_1280x720());
  check(view.has_value() && view->rows.size() == 3u,
        "the skills tab is one row per skill");
  if (!view || view->rows.size() != 3u) {
    return;
  }
  check(view->rows[0].value == "Special, rank 1/20",
        "a skill's type and its owned of all ranks");
  check(view->rows[1].value == "Beam, rank 2/18",
        "filler is no rank, and a rank added this visit is owned");
  check(view->rows[2].value == "Trap, Req: Level 14, Magnetic Shell",
        "a locked skill shows what it needs");
  check(view->facts.size() == 2u && view->facts[1].value == "37",
        "the skill points left");
  check(view->focused_row == 0 && view->rows[0].press_on_arrival &&
            !view->rows[1].press_on_arrival,
        "a tap on the selected skill presses A, which adds a rank");
  check(x2::input::skill_rank_text(kLocked + "x") == std::nullopt &&
            x2::input::skill_rank_text(glyphs(kFiller, 3)) == std::nullopt &&
            x2::input::skill_rank_text("") == std::nullopt,
        "text that is not a run of rank glyphs is not a rank");
}

void the_gear_tab_walks_the_focused_list() {
  const RetailScenePlane plane = plane_1280x720();
  const auto slots = x2::input::build_touch_menu_view(gear_menu(false), plane);
  check(slots.has_value() && slots->rows.size() == 3u,
        "the gear tab lists the three slots while they have the focus");
  if (!slots || slots->rows.size() != 3u) {
    return;
  }
  check(slots->rows[0].label == "Fortified Waistband" &&
            slots->rows[1].label == "Nothing Equipped",
        "an equipped piece by name, an empty slot without the dashes");
  check(slots->facts.size() == 2u && slots->facts[1].label == "gear" &&
            slots->facts[1].value == "1/20",
        "the gear count the game shows");
  check(has_line(*slots, "3 DR. +3 EP per Knockout (Level 20)"),
        "the selected piece's description");
  const auto inventory =
      x2::input::build_touch_menu_view(gear_menu(true), plane);
  check(inventory && inventory->rows.size() == 2u &&
            inventory->rows[0].label == "Coil of Swiftness" &&
            inventory->focused_row == 0,
        "the pieces that fit the slot once A moved the focus to them");

  std::size_t drop = slots->footers.size();
  for (std::size_t i = 0; i < slots->footers.size(); ++i) {
    if (slots->footers[i].token == "$MENU_DROP") {
      drop = i;
    }
  }
  check(drop < slots->footers.size() &&
            slots->footers[drop].button == TouchAction::MenuRightShoulder &&
            !slots->footers[0].button,
        "Drop is the pad's RB, which the text item's click does not publish");
  TouchMenu touch;
  touch.set_viewport(viewport_1280x720());
  touch.set_view(slots, 0u);
  const TouchMenuButton *button =
      find(touch.layout(), TouchMenuPart::footer, static_cast<int>(drop));
  const auto out = button == nullptr ? std::vector<TouchMenuDelivery>{}
                                     : tap(touch, button->rect, 10u);
  check(out.size() == 1u && out[0].kind == TouchMenuDelivery::Kind::pad &&
            out[0].button == TouchAction::MenuRightShoulder,
        "a tap on Drop presses RB");
  const TouchMenuButton *unequip =
      find(touch.layout(), TouchMenuPart::footer, 0);
  const auto clicked = unequip == nullptr ? std::vector<TouchMenuDelivery>{}
                                          : tap(touch, unequip->rect, 20u);
  check(clicked.size() == 1u &&
            clicked[0].kind == TouchMenuDelivery::Kind::click,
        "Unequip is the game's own footer click");
}

} // namespace

int main() {
  the_party_is_its_heroes();
  the_roster_is_its_named_heroes();
  the_stats_tab_walks_to_a_stat_and_adds_on_it();
  the_ai_tab_walks_its_settings();
  the_skills_tab_reads_ranks();
  the_gear_tab_walks_the_focused_list();
  return report("touch_menu_team");
}
