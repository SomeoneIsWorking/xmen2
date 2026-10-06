#include "touch_menu_parts.hpp"

#include <cctype>
#include <string_view>

namespace x2::input {
namespace {

constexpr std::string_view kTokenPrefix = "$MENU_";
constexpr std::string_view kFooterPrefix = "desctext";
constexpr std::string_view kTitlePrefix = "title";

bool is_space(char c) { return std::isspace(static_cast<unsigned char>(c)); }

std::optional<TouchMenuFooter>
footer_of(const menu::MenuItem &item,
          const presentation::RetailScenePlane &plane) {
  if (!item.name.starts_with(kFooterPrefix) || !menu_item_shown(item)) {
    return std::nullopt;
  }
  const std::size_t at = item.label.find(kTokenPrefix);
  if (at == std::string::npos) {
    return std::nullopt;
  }
  std::size_t end = at;
  while (end < item.label.size() && !is_space(item.label[end])) {
    ++end;
  }
  TouchMenuFooter footer;
  footer.token = item.label.substr(at, end - at);
  footer.label = touch_menu_text(item.label);
  footer.click = menu_item_centre(item.rect, plane);
  return footer;
}

} // namespace

presentation::ClientPoint
menu_item_centre(const menu::SceneRect &rect,
                 const presentation::RetailScenePlane &plane) {
  return plane.to_client({0.5F * static_cast<float>(rect.left + rect.right),
                          0.5F * static_cast<float>(rect.top + rect.bottom)});
}

std::vector<std::string> menu_text_lines(std::string_view raw) {
  std::vector<std::string> lines;
  std::size_t start = 0;
  while (start <= raw.size()) {
    std::size_t end = raw.find('\n', start);
    if (end == std::string_view::npos) {
      end = raw.size();
    }
    std::string line = touch_menu_text(raw.substr(start, end - start));
    if (!line.empty()) {
      lines.push_back(std::move(line));
    }
    start = end + 1u;
  }
  return lines;
}

const menu::MenuItem *find_menu_item(const menu::MenuSnapshot &menu,
                                     std::string_view name) {
  for (const menu::MenuItem &item : menu.items) {
    if (item.name == name) {
      return &item;
    }
  }
  return nullptr;
}

bool menu_item_shown(const menu::MenuItem &item) {
  return item.enabled() && !item.hidden();
}

std::string menu_title(const menu::MenuSnapshot &menu) {
  const std::string own = "label_" + menu.name;
  for (const menu::MenuItem &item : menu.items) {
    if (item.name == own && !item.hidden()) {
      const std::string text = touch_menu_text(item.label);
      if (!text.empty()) {
        return text;
      }
    }
  }
  for (const menu::MenuItem &item : menu.items) {
    if (item.name.starts_with(kTitlePrefix) && !item.hidden()) {
      const std::string text = touch_menu_text(item.label);
      if (!text.empty()) {
        return text;
      }
    }
  }
  return {};
}

void append_menu_footers(const menu::MenuSnapshot &menu,
                         const presentation::RetailScenePlane &plane,
                         TouchMenuView *view) {
  for (const menu::MenuItem &item : menu.items) {
    if (auto footer = footer_of(item, plane)) {
      view->footers.push_back(std::move(*footer));
    }
  }
}

void append_list_entries(const menu::MenuItem &list,
                         const presentation::RetailScenePlane &plane,
                         ListTap tap, TouchMenuView *view) {
  const menu::ListBoxState &box = *list.list_box;
  view->focus_wraps = false;
  const float centre_x =
      0.5F * static_cast<float>(box.hit.left + box.hit.right);
  for (std::size_t i = 0; i < box.entries.size(); ++i) {
    const int entry = static_cast<int>(i);
    const int window_row = entry - box.top;
    TouchMenuRow row;
    row.label = touch_menu_text(box.entries[i]);
    row.clicks = tap == ListTap::select && box.row_height > 0 &&
                 window_row >= 0 && window_row < box.visible_rows;
    row.press_on_arrival = tap == ListTap::accept;
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

TouchMenuView start_menu_view(const menu::MenuSnapshot &menu) {
  TouchMenuView view;
  view.address = menu.address;
  view.menu = menu.name;
  view.menu_class = menu.menu_class;
  view.title = menu_title(menu);
  return view;
}

} // namespace x2::input
