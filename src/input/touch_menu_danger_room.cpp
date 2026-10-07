#include "touch_menu_danger_room.hpp"

#include "touch_menu_parts.hpp"

#include <array>
#include <string_view>

namespace x2::input {
namespace {

constexpr std::array<std::string_view, 3> kDangerRoomTabs = {
    "option_text1", "option_text2", "option_text3"};
constexpr std::string_view kDangerRoomList = "list";
constexpr std::string_view kDangerRoomDescription = "desc";

} // namespace

std::optional<TouchMenuView>
build_danger_room_view(const menu::MenuSnapshot &menu,
                       const presentation::RetailScenePlane &plane) {
  const menu::MenuItem *list = find_menu_item(menu, kDangerRoomList);
  if (list == nullptr || !list->list_box || !menu_item_shown(*list)) {
    return std::nullopt;
  }
  TouchMenuView view = start_menu_view(menu);
  append_menu_tabs(menu, plane, kDangerRoomTabs, &view);
  append_list_entries(*list, ListTap::select, &view);
  const menu::MenuItem *description =
      find_menu_item(menu, kDangerRoomDescription);
  if (description != nullptr && menu_item_shown(*description)) {
    view.detail = menu_text_lines(description->label);
  }
  append_menu_footers(menu, plane, &view);
  return view;
}

} // namespace x2::input
