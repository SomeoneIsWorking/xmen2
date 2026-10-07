#include "touch_menu_team.hpp"

#include "touch_menu_parts.hpp"

#include <array>
#include <string_view>

namespace x2::input {
namespace {

constexpr std::uint32_t kPartyMode = 0u;
constexpr std::uint32_t kRosterMode = 1u;
constexpr std::array<std::string_view, 4> kPartySummaries = {
    "char_summary01", "char_summary02", "char_summary03", "char_summary04"};
/* The roster's carousel list and its middle card, the one A acts on. */
constexpr std::string_view kRosterList = "roster_portrait01";
constexpr std::string_view kRosterMiddleCard = "roster_summary02";

std::string hero_level(const native::HeroRecord &hero) {
  return (hero.fallen ? "Fallen, level " : "Level ") +
         std::to_string(hero.level);
}

std::optional<TouchMenuView>
build_party_view(const menu::MenuSnapshot &menu,
                 const presentation::RetailScenePlane &plane) {
  TouchMenuView view = start_menu_view(menu);
  for (const std::string_view name : kPartySummaries) {
    const menu::MenuItem *item = find_menu_item(menu, name);
    if (item == nullptr || !menu_item_shown(*item)) {
      continue;
    }
    TouchMenuRow row;
    row.label = item->hero            ? item->hero->display_name
                : item->label.empty() ? std::string("Empty slot")
                                      : touch_menu_text(item->label);
    if (item->hero) {
      row.value = hero_level(*item->hero);
    }
    row.clicks = true;
    /* The selected hero's summary is lit (item+0x54 bit 0). */
    row.focused = (item->flags & menu::kItemFocusLit) != 0u;
    row.slot = item->slot;
    row.click = menu_item_centre(item->rect, plane);
    if (row.focused) {
      view.focused_row = static_cast<int>(view.rows.size());
    }
    view.rows.push_back(std::move(row));
  }
  if (view.rows.empty()) {
    return std::nullopt;
  }
  append_menu_footers(menu, plane, &view);
  return view;
}

/* One row per hero the cards would name; the pad's Up/Down turns the
   carousel one entry, wrapping through the locked ones too, and A on the
   middle card chooses or revives that hero (FUN_005c29e0 puts the middle card
   at +0xac + 1). */
std::optional<TouchMenuView>
build_roster_view(const menu::MenuSnapshot &menu,
                  const presentation::RetailScenePlane &plane) {
  const menu::MenuItem *list = find_menu_item(menu, kRosterList);
  const menu::MenuItem *middle = find_menu_item(menu, kRosterMiddleCard);
  if (list == nullptr || !list->list_box || middle == nullptr ||
      list->heroes.size() != list->list_box->entries.size()) {
    return std::nullopt;
  }
  const int count = static_cast<int>(list->heroes.size());
  if (count == 0) {
    return std::nullopt;
  }
  const int selected = (list->list_box->selected + 1) % count;
  TouchMenuView view = start_menu_view(menu);
  view.cycle = count;
  view.cycle_focus = selected;
  for (int i = 0; i < count; ++i) {
    const std::optional<native::HeroRecord> &hero =
        list->heroes[static_cast<std::size_t>(i)];
    if (!hero || (!hero->unlocked && middle->masks_locked)) {
      continue;
    }
    TouchMenuRow row;
    row.label = hero->display_name;
    row.value = hero_level(*hero);
    row.slot = list->slot;
    row.entry = i;
    row.focused = i == selected;
    if (row.focused) {
      view.focused_row = static_cast<int>(view.rows.size());
    }
    view.rows.push_back(std::move(row));
  }
  if (view.rows.empty()) {
    return std::nullopt;
  }
  append_menu_footers(menu, plane, &view);
  return view;
}

} // namespace

std::optional<TouchMenuView>
build_team_view(const menu::MenuSnapshot &menu,
                const presentation::RetailScenePlane &plane) {
  if (!menu.mode) {
    return std::nullopt;
  }
  if (*menu.mode == kPartyMode) {
    return build_party_view(menu, plane);
  }
  if (*menu.mode == kRosterMode) {
    return build_roster_view(menu, plane);
  }
  return std::nullopt;
}

} // namespace x2::input
