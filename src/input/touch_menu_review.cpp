#include "touch_menu_review.hpp"

#include "touch_menu_parts.hpp"

#include <array>
#include <string_view>

namespace x2::input {
namespace {

constexpr std::array<std::string_view, 5> kReviewTabs = {
    "option01_text", "option02_text", "option03_text", "option04_text",
    "option05_text"};
constexpr std::string_view kReviewList = "list";

} // namespace

std::optional<TouchMenuView>
build_review_view(const menu::MenuSnapshot &menu,
                  const presentation::RetailScenePlane &plane) {
  const menu::MenuItem *list = find_menu_item(menu, kReviewList);
  if (list == nullptr || !list->list_box || !menu_item_shown(*list)) {
    return std::nullopt;
  }
  TouchMenuView view = start_menu_view(menu);
  append_menu_tabs(menu, plane, kReviewTabs, &view);
  append_list_entries(*list, ListTap::select, &view);
  append_menu_footers(menu, plane, &view);
  return view;
}

} // namespace x2::input
