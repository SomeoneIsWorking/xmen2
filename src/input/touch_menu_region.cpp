#include "touch_menu_region.hpp"

#include "touch_menu_parts.hpp"

#include <string_view>

namespace x2::input {
namespace {

constexpr std::string_view kRegionList = "text_list";
constexpr std::string_view kRegionTitle = "text_title";

} // namespace

std::optional<TouchMenuView>
build_region_view(const menu::MenuSnapshot &menu,
                  const presentation::RetailScenePlane &plane) {
  const menu::MenuItem *list = find_menu_item(menu, kRegionList);
  if (list == nullptr || !list->list_box || !menu_item_shown(*list)) {
    return std::nullopt;
  }
  TouchMenuView view = start_menu_view(menu);
  const menu::MenuItem *title = find_menu_item(menu, kRegionTitle);
  if (title != nullptr && menu_item_shown(*title)) {
    view.title = touch_menu_text(title->label);
  }
  append_list_entries(*list, ListTap::select, &view);
  append_menu_footers(menu, plane, &view);
  return view;
}

} // namespace x2::input
