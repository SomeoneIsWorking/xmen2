#include "touch_menu_team.hpp"

#include "touch_menu_parts.hpp"

#include <array>
#include <string_view>

namespace x2::input {
namespace {

constexpr std::uint32_t kPartyMode = 0u;
constexpr std::array<std::string_view, 4> kPartySummaries = {
    "char_summary01", "char_summary02", "char_summary03", "char_summary04"};

} // namespace

std::optional<TouchMenuView>
build_team_view(const menu::MenuSnapshot &menu,
                const presentation::RetailScenePlane &plane) {
  if (!menu.mode || *menu.mode != kPartyMode) {
    return std::nullopt;
  }
  TouchMenuView view = start_menu_view(menu);
  for (const std::string_view name : kPartySummaries) {
    const menu::MenuItem *item = find_menu_item(menu, name);
    if (item == nullptr || !menu_item_shown(*item)) {
      continue;
    }
    TouchMenuRow row;
    row.label = touch_menu_text(item->label);
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

} // namespace x2::input
