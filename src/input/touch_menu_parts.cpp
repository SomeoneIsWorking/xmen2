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

TouchMenuView start_menu_view(const menu::MenuSnapshot &menu) {
  TouchMenuView view;
  view.address = menu.address;
  view.menu = menu.name;
  view.menu_class = menu.menu_class;
  view.title = menu_title(menu);
  return view;
}

} // namespace x2::input
