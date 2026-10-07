#include "touch_menu_team_details.hpp"

#include "touch_menu_parts.hpp"

#include <array>
#include <cctype>
#include <string_view>

namespace x2::input {
namespace {

constexpr std::uint32_t kStatsMode = 2u;
constexpr std::uint32_t kSkillsMode = 3u;
constexpr std::uint32_t kGearMode = 4u;
constexpr std::uint32_t kAiMode = 5u;
constexpr std::uint32_t kSkillsAssignMode = 6u;

constexpr std::array<std::string_view, 4> kDetailTabs = {
    "detail_option01_text", "detail_option02_text", "detail_option03_text",
    "detail_option04_text"};

/* A row the game walks with Up/Down: the label it lights and the value it
   shows. */
struct LabelledValue {
  std::string_view label;
  std::string_view value;
};

constexpr std::array<LabelledValue, 4> kStats = {{
    {"label_body", "body"},
    {"label_focus", "focus"},
    {"label_strike", "strike"},
    {"label_speed", "speed"},
}};
constexpr std::array<LabelledValue, 5> kDerivedStats = {{
    {"label_hp", "health"},
    {"label_ep", "energy"},
    {"label_dmg", "damage"},
    {"label_atk", "attack"},
    {"label_def", "defense"},
}};
constexpr std::array<LabelledValue, 7> kAiSettings = {{
    {"label_ai_heal_pickup", "ai_heal_pickup"},
    {"label_ai_heal", "ai_heal"},
    {"label_ai_level", "ai_level"},
    {"label_ai_power", "ai_power"},
    {"label_ai_auto_traits", "ai_auto_traits"},
    {"label_ai_auto_skills", "ai_auto_skills"},
    {"label_ai_auto_equip", "ai_auto_equip"},
}};
constexpr std::array<std::string_view, 2> kGearLists = {"equipment",
                                                        "equipment_inv"};
/* An empty slot reads "--- [ Nothing Equipped ] ---". */
constexpr std::string_view kEmptySlotOpen = "--- [ ";
constexpr std::string_view kEmptySlotClose = " ] ---";

/* FUN_005f01e0's rank glyphs, read as Latin-1 into UTF-8 (C3 xx): one per
   rank, after 0xdb filler that pads a short skill to twenty places. */
constexpr char kGlyphLead = '\xc3';
constexpr char kRankLocked = '\x97';
constexpr char kRankOpen = '\x98';
constexpr char kRankOwned = '\x99';
constexpr char kRankAdded = '\x9a';
constexpr char kRankFiller = '\x9b';

/* The stat description's resistance legend: "($RES_ENERGY)" draws the icon
   that label_re draws beside resist_energy. */
constexpr std::string_view kResistTokenOpen = "($RES_";
constexpr std::string_view kResistItemPrefix = "resist_";

std::string shown_text(const menu::MenuSnapshot &menu, std::string_view name) {
  const menu::MenuItem *item = find_menu_item(menu, name);
  if (item == nullptr || !menu_item_shown(*item)) {
    return {};
  }
  return touch_menu_text(item->label);
}

void append_fact(const menu::MenuSnapshot &menu, std::string label,
                 std::string_view value, TouchMenuView *view) {
  const std::string text = shown_text(menu, value);
  if (!label.empty() && !text.empty()) {
    view->facts.push_back({std::move(label), text, false});
  }
}

/* A click anywhere on these rows moves the game to the row and then acts on
   it, so a tap walks to the row and only a tap on the current row presses A
   (add a point, change the setting). */
void append_labelled_rows(const menu::MenuSnapshot &menu,
                          std::span<const LabelledValue> rows,
                          TouchMenuView *view) {
  for (const LabelledValue &entry : rows) {
    const menu::MenuItem *label = find_menu_item(menu, entry.label);
    if (label == nullptr || !menu_item_shown(*label)) {
      continue;
    }
    TouchMenuRow row;
    row.label = touch_menu_text(label->label);
    row.value = shown_text(menu, entry.value);
    row.focused = (label->flags & menu::kItemFocusLit) != 0u;
    row.press_on_arrival = row.focused;
    row.slot = label->slot;
    if (row.focused) {
      view->focused_row = static_cast<int>(view->rows.size());
    }
    view->rows.push_back(std::move(row));
  }
}

/* A description line naming a resistance by its icon token ("Energy
   Resistance ($RES_ENERGY)") with the value the icon labels
   (resist_energy) in the icon's place. */
std::string stat_description_line(const menu::MenuSnapshot &menu,
                                  std::string_view raw) {
  const std::size_t open = raw.find(kResistTokenOpen);
  const std::size_t close =
      open == std::string_view::npos ? open : raw.find(')', open);
  if (close == std::string_view::npos) {
    return touch_menu_text(raw);
  }
  std::string item(kResistItemPrefix);
  for (const char c : raw.substr(open + kResistTokenOpen.size(),
                                 close - open - kResistTokenOpen.size())) {
    item.push_back(
        static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  }
  const std::string value = shown_text(menu, item);
  const std::string name = touch_menu_text(raw.substr(0, open));
  return value.empty() ? name : name + " " + value;
}

void build_stats(const menu::MenuSnapshot &menu, TouchMenuView *view) {
  append_labelled_rows(menu, kStats, view);
  append_fact(menu, shown_text(menu, "label_statpoints"), "points_stats", view);
  for (const LabelledValue &entry : kDerivedStats) {
    std::string line = shown_text(menu, entry.label);
    const std::string value = shown_text(menu, entry.value);
    if (!line.empty() && !value.empty()) {
      line += ' ';
      line += value;
      view->detail.push_back(std::move(line));
    }
  }
  const menu::MenuItem *description = find_menu_item(menu, "stat_desc");
  if (description == nullptr || !menu_item_shown(*description)) {
    return;
  }
  const std::string_view raw = description->label;
  for (std::size_t start = 0; start <= raw.size();) {
    std::size_t end = raw.find('\n', start);
    if (end == std::string_view::npos) {
      end = raw.size();
    }
    std::string line =
        stat_description_line(menu, raw.substr(start, end - start));
    if (!line.empty()) {
      view->detail.push_back(std::move(line));
    }
    start = end + 1u;
  }
}

void build_skills(const menu::MenuSnapshot &menu, TouchMenuView *view) {
  const menu::MenuItem *list = find_menu_item(menu, "skill_list");
  if (list == nullptr || !list->list_box || !menu_item_shown(*list)) {
    return;
  }
  append_list_entries(*list, ListTap::select, view);
  const std::vector<std::vector<std::string>> &columns =
      list->list_box->columns;
  for (TouchMenuRow &row : view->rows) {
    const auto entry = static_cast<std::size_t>(row.entry);
    if (entry >= columns.size()) {
      continue;
    }
    std::string value;
    for (const std::string &column : columns[entry]) {
      const std::optional<std::string> rank = skill_rank_text(column);
      const std::string text = rank ? *rank : touch_menu_text(column);
      if (text.empty()) {
        continue;
      }
      value += value.empty() ? text : ", " + text;
    }
    row.value = std::move(value);
  }
  append_fact(menu, shown_text(menu, "label_skillpoints"), "points_skills",
              view);
}

/* The game walks one gear list at a time: the slots, or the pieces that fit
   the chosen slot. */
void build_gear(const menu::MenuSnapshot &menu, TouchMenuView *view) {
  for (const std::string_view name : kGearLists) {
    const menu::MenuItem *list = find_menu_item(menu, name);
    if (list != nullptr && list->list_box && list->focused &&
        menu_item_shown(*list)) {
      append_list_entries(*list, ListTap::select, view);
    }
  }
  for (TouchMenuRow &row : view->rows) {
    const std::string_view label = row.label;
    if (label.size() > kEmptySlotOpen.size() + kEmptySlotClose.size() &&
        label.starts_with(kEmptySlotOpen) && label.ends_with(kEmptySlotClose)) {
      row.label = std::string(label.substr(
          kEmptySlotOpen.size(),
          label.size() - kEmptySlotOpen.size() - kEmptySlotClose.size()));
    }
  }
  append_fact(menu, shown_text(menu, "inventory_count_title"),
              "inventory_count", view);
  const menu::MenuItem *description = find_menu_item(menu, "equipment_desc");
  if (description != nullptr && menu_item_shown(*description)) {
    view->detail = menu_text_lines(description->label);
  }
}

void build_ai(const menu::MenuSnapshot &menu, TouchMenuView *view) {
  append_labelled_rows(menu, kAiSettings, view);
  const menu::MenuItem *description = find_menu_item(menu, "ai_desc");
  if (description != nullptr && menu_item_shown(*description)) {
    view->detail = menu_text_lines(description->label);
  }
}

} // namespace

std::optional<std::string> skill_rank_text(std::string_view column) {
  if (column.empty() || column.size() % 2u != 0u) {
    return std::nullopt;
  }
  int ranks = 0;
  int owned = 0;
  for (std::size_t at = 0; at < column.size(); at += 2u) {
    if (column[at] != kGlyphLead) {
      return std::nullopt;
    }
    const char glyph = column[at + 1u];
    if (glyph == kRankOwned || glyph == kRankAdded) {
      ++owned;
    } else if (glyph != kRankOpen && glyph != kRankLocked) {
      if (glyph != kRankFiller) {
        return std::nullopt;
      }
      continue;
    }
    ++ranks;
  }
  if (ranks == 0) {
    return std::nullopt;
  }
  return "rank " + std::to_string(owned) + "/" + std::to_string(ranks);
}

std::optional<TouchMenuView>
build_team_details_view(const menu::MenuSnapshot &menu,
                        const presentation::RetailScenePlane &plane) {
  if (!menu.mode) {
    return std::nullopt;
  }
  TouchMenuView view = start_menu_view(menu);
  view.title = shown_text(menu, "name");
  append_menu_tabs(menu, plane, kDetailTabs, &view);
  append_fact(menu, "Level", "level", &view);
  switch (*menu.mode) {
  case kStatsMode:
    build_stats(menu, &view);
    break;
  case kSkillsMode:
  case kSkillsAssignMode:
    build_skills(menu, &view);
    break;
  case kGearMode:
    build_gear(menu, &view);
    break;
  case kAiMode:
    build_ai(menu, &view);
    break;
  default:
    return std::nullopt;
  }
  if (view.tabs.empty()) {
    return std::nullopt;
  }
  append_menu_footers(menu, plane, &view);
  return view;
}

} // namespace x2::input
