#include "touch_menu_worldmap.hpp"

#include "touch_menu_parts.hpp"

#include <array>
#include <string_view>

namespace x2::input {
namespace {

constexpr std::array<std::string_view, 5> kActTabs = {
    "option01_text", "option02_text", "option03_text", "option04_text",
    "option05_text"};
/* The list window's rows; the game fills them from the open act's points
   and enables the unlocked ones (0x005c4620). */
constexpr std::array<std::string_view, 7> kPointRows = {
    "list01_01", "list01_02", "list01_03", "list01_04",
    "list01_05", "list01_06", "list01_07"};
constexpr std::string_view kRegion = "map_title";
constexpr std::string_view kDescription = "extract_description";

int item_index(const menu::MenuSnapshot &menu, std::string_view name) {
  for (std::size_t i = 0; i < menu.items.size(); ++i) {
    if (menu.items[i].name == name) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

void append_points(const menu::MenuSnapshot &menu, TouchMenuView *view) {
  for (const std::string_view name : kPointRows) {
    const int index = item_index(menu, name);
    if (index < 0) {
      continue;
    }
    const menu::MenuItem &item = menu.items[static_cast<std::size_t>(index)];
    if (!menu_item_shown(item)) {
      continue;
    }
    TouchMenuRow row;
    row.label = touch_menu_text(item.label);
    row.focused = index == menu.focused;
    /* A on the focused point is MENU_ACCEPT, which travels; a walk to
       another point only selects it. */
    row.press_on_arrival = row.focused;
    row.slot = item.slot;
    if (row.focused) {
      view->focused_row = static_cast<int>(view->rows.size());
    }
    view->rows.push_back(std::move(row));
  }
}

void append_place(const menu::MenuSnapshot &menu, TouchMenuView *view) {
  if (const menu::MenuItem *region = find_menu_item(menu, kRegion);
      region != nullptr && menu_item_shown(*region)) {
    TouchMenuFact fact;
    fact.value = touch_menu_text(region->label);
    if (!fact.value.empty()) {
      view->facts.push_back(std::move(fact));
    }
  }
  if (const menu::MenuItem *description = find_menu_item(menu, kDescription);
      description != nullptr && menu_item_shown(*description)) {
    view->detail = menu_text_lines(description->label);
  }
}

} // namespace

std::optional<TouchMenuView>
build_worldmap_view(const menu::MenuSnapshot &menu,
                    const presentation::RetailScenePlane &plane) {
  TouchMenuView view = start_menu_view(menu);
  append_menu_tabs(menu, plane, kActTabs, &view);
  append_points(menu, &view);
  if (view.rows.empty()) {
    return std::nullopt;
  }
  view.focus_wraps = false;
  append_place(menu, &view);
  append_menu_footers(menu, plane, &view);
  return view;
}

} // namespace x2::input
