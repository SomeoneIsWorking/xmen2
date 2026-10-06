#include "touch_menu_codex.hpp"

#include "touch_menu_parts.hpp"

#include <string_view>

namespace x2::input {
namespace {

constexpr std::uint32_t kListMode = 0u;
constexpr std::uint32_t kReadMode = 1u;
constexpr std::string_view kCodexList = "list";
constexpr std::string_view kDescription = "desc";
constexpr std::string_view kEntryName = "name";

std::optional<TouchMenuView>
build_list(const menu::MenuSnapshot &menu,
           const presentation::RetailScenePlane &plane) {
  const menu::MenuItem *list = find_menu_item(menu, kCodexList);
  if (list == nullptr || !list->list_box || !menu_item_shown(*list)) {
    return std::nullopt;
  }
  TouchMenuView view = start_menu_view(menu);
  /* The codex loads an entry only on MENU_ACCEPT (0x005b1780). */
  append_list_entries(*list, plane, ListTap::accept, &view);
  if (view.rows.empty()) {
    return std::nullopt;
  }
  append_menu_footers(menu, plane, &view);
  return view;
}

std::optional<TouchMenuView>
build_reading(const menu::MenuSnapshot &menu,
              const presentation::RetailScenePlane &plane) {
  const menu::MenuItem *description = find_menu_item(menu, kDescription);
  if (description == nullptr || !menu_item_shown(*description)) {
    return std::nullopt;
  }
  TouchMenuView view = start_menu_view(menu);
  if (const menu::MenuItem *name = find_menu_item(menu, kEntryName);
      name != nullptr && menu_item_shown(*name)) {
    view.title = touch_menu_text(name->label);
  }
  view.detail = menu_text_lines(description->label);
  if (view.detail.empty()) {
    return std::nullopt;
  }
  append_menu_footers(menu, plane, &view);
  return view;
}

} // namespace

std::optional<TouchMenuView>
build_codex_view(const menu::MenuSnapshot &menu,
                 const presentation::RetailScenePlane &plane) {
  if (menu.mode == kListMode) {
    return build_list(menu, plane);
  }
  if (menu.mode == kReadMode) {
    return build_reading(menu, plane);
  }
  return std::nullopt;
}

} // namespace x2::input
