#include "touch_menu_shop.hpp"

#include "touch_menu_parts.hpp"

#include <array>
#include <string_view>

namespace x2::input {
namespace {

constexpr std::uint32_t kStashBit = 0x01u;
constexpr std::array<std::string_view, 3> kShopTabs = {
    "shop_option01", "shop_option02", "shop_option03"};
constexpr std::string_view kShopList = "list";

void append_tabs(const menu::MenuSnapshot &menu,
                 const presentation::RetailScenePlane &plane,
                 TouchMenuView *view) {
  for (const std::string_view name : kShopTabs) {
    const menu::MenuItem *tab = find_menu_item(menu, name);
    if (tab == nullptr || !menu_item_shown(*tab)) {
      continue;
    }
    TouchMenuRow row;
    row.label = touch_menu_text(tab->label);
    row.clicks = true;
    /* The open tab is lit (item+0x54 bit 0). */
    row.focused = (tab->flags & menu::kItemFocusLit) != 0u;
    row.slot = tab->slot;
    row.click = menu_item_centre(tab->rect, plane);
    view->rows.push_back(std::move(row));
  }
}

void append_entries(const menu::MenuItem &list,
                    const presentation::RetailScenePlane &plane,
                    TouchMenuView *view) {
  const menu::ListBoxState &box = *list.list_box;
  const float centre_x =
      0.5F * static_cast<float>(box.hit.left + box.hit.right);
  for (std::size_t i = 0; i < box.entries.size(); ++i) {
    const int entry = static_cast<int>(i);
    const int window_row = entry - box.top;
    TouchMenuRow row;
    row.label = touch_menu_text(box.entries[i]);
    row.clicks =
        box.row_height > 0 && window_row >= 0 && window_row < box.visible_rows;
    row.press_on_arrival = false;
    row.focused = entry == box.selected;
    row.slot = list.slot;
    row.entry = entry;
    if (row.clicks) {
      /* The list counts rows down from its box's top edge. */
      const float y = static_cast<float>(box.hit.bottom) -
                      static_cast<float>(window_row * box.row_height) -
                      0.5F * static_cast<float>(box.row_height);
      row.click = plane.to_client({centre_x, y});
    }
    if (row.focused) {
      view->focused_row = static_cast<int>(view->rows.size());
    }
    view->rows.push_back(std::move(row));
  }
}

} // namespace

std::optional<TouchMenuView>
build_shop_view(const menu::MenuSnapshot &menu,
                const presentation::RetailScenePlane &plane) {
  if (!menu.mode || (*menu.mode & kStashBit) != 0u) {
    return std::nullopt;
  }
  const menu::MenuItem *list = find_menu_item(menu, kShopList);
  if (list == nullptr || !list->list_box || !menu_item_shown(*list)) {
    return std::nullopt;
  }
  TouchMenuView view = start_menu_view(menu);
  view.focus_wraps = false;
  append_tabs(menu, plane, &view);
  append_entries(*list, plane, &view);
  append_menu_footers(menu, plane, &view);
  return view;
}

} // namespace x2::input
