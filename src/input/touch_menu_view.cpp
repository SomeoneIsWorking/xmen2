#include "touch_menu_view.hpp"

#include <array>
#include <cctype>

namespace x2::input {
namespace {

constexpr std::array<std::string_view, 3> kReplacedClasses = {
    "CMenuMain", "CMenuOptions", "CMenuPDA"};

constexpr std::string_view kTokenPrefix = "$MENU_";
constexpr std::string_view kFooterPrefix = "desctext";
constexpr std::string_view kTitlePrefix = "title";

bool is_space(char c) { return std::isspace(static_cast<unsigned char>(c)); }

bool is_digit(char c) { return std::isdigit(static_cast<unsigned char>(c)); }

/* "r i s e   o f" -> "rise of": every other character a single space. */
bool letter_spaced(std::string_view text) {
  if (text.size() < 3u) {
    return false;
  }
  for (std::size_t i = 1; i < text.size(); i += 2u) {
    if (text[i] != ' ') {
      return false;
    }
  }
  return true;
}

std::string close_letter_spacing(std::string_view text) {
  std::string out;
  std::size_t spaces = 0;
  for (const char c : text) {
    if (c == ' ') {
      ++spaces;
      continue;
    }
    if (spaces >= 3u && !out.empty()) {
      out.push_back(' ');
    }
    spaces = 0;
    out.push_back(c);
  }
  return out;
}

presentation::ClientPoint centre(const menu::SceneRect &rect,
                                 const presentation::RetailScenePlane &plane) {
  return plane.to_client({0.5F * static_cast<float>(rect.left + rect.right),
                          0.5F * static_cast<float>(rect.top + rect.bottom)});
}

bool shown(const menu::MenuItem &item) {
  return item.enabled() && !item.hidden();
}

std::string title_of(const menu::MenuSnapshot &menu) {
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

std::optional<TouchMenuFooter>
footer_of(const menu::MenuItem &item,
          const presentation::RetailScenePlane &plane) {
  if (!item.name.starts_with(kFooterPrefix) || !shown(item)) {
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
  footer.click = centre(item.rect, plane);
  return footer;
}

} // namespace

bool TouchMenuView::same_screen(const TouchMenuView &other) const {
  if (address != other.address || menu != other.menu ||
      rows.size() != other.rows.size()) {
    return false;
  }
  for (std::size_t i = 0; i < rows.size(); ++i) {
    if (rows[i].slot != other.rows[i].slot) {
      return false;
    }
  }
  return true;
}

bool touch_menu_replaces(std::string_view menu_class) {
  for (const std::string_view replaced : kReplacedClasses) {
    if (menu_class == replaced) {
      return true;
    }
  }
  return false;
}

std::optional<TouchMenuView>
build_touch_menu_view(const menu::MenuSnapshot &menu,
                      const presentation::RetailScenePlane &plane) {
  if (menu.popup_up || !touch_menu_replaces(menu.menu_class)) {
    return std::nullopt;
  }
  TouchMenuView view;
  view.address = menu.address;
  view.menu = menu.name;
  view.menu_class = menu.menu_class;
  view.title = title_of(menu);
  for (const int index : menu.rows) {
    const menu::MenuItem &item = menu.items[static_cast<std::size_t>(index)];
    if (!shown(item)) {
      continue;
    }
    TouchMenuRow row;
    row.label = touch_menu_text(item.label);
    row.fill = item.fill;
    if (item.value_item >= 0) {
      const menu::MenuItem &value =
          menu.items[static_cast<std::size_t>(item.value_item)];
      row.value = touch_menu_text(value.label);
      if (value.fill) {
        row.fill = value.fill;
      }
    }
    row.clicks = !item.use_command.empty();
    row.steps = item.has_left_right();
    row.focused = index == menu.focused;
    row.slot = item.slot;
    row.click = centre(item.rect, plane);
    if (row.focused) {
      view.focused_row = static_cast<int>(view.rows.size());
    }
    view.rows.push_back(std::move(row));
  }
  if (view.rows.empty()) {
    return std::nullopt;
  }
  for (const menu::MenuItem &item : menu.items) {
    if (auto footer = footer_of(item, plane)) {
      view.footers.push_back(std::move(*footer));
    }
  }
  return view;
}

std::string touch_menu_text(std::string_view raw) {
  std::string kept;
  for (std::size_t i = 0; i < raw.size();) {
    if (raw[i] == '~' && i + 2u < raw.size() && is_digit(raw[i + 1u]) &&
        is_digit(raw[i + 2u])) {
      i += 3u;
      continue;
    }
    if (raw[i] == '$') {
      while (i < raw.size() && !is_space(raw[i])) {
        ++i;
      }
      continue;
    }
    kept.push_back(raw[i]);
    ++i;
  }
  std::size_t first = 0;
  while (first < kept.size() && is_space(kept[first])) {
    ++first;
  }
  std::size_t last = kept.size();
  while (last > first && is_space(kept[last - 1u])) {
    --last;
  }
  const std::string_view trimmed(kept.data() + first, last - first);
  if (letter_spaced(trimmed)) {
    return close_letter_spacing(trimmed);
  }
  std::string out;
  bool space = false;
  for (const char c : trimmed) {
    if (is_space(c)) {
      space = true;
      continue;
    }
    if (space) {
      out.push_back(' ');
      space = false;
    }
    out.push_back(c);
  }
  return out;
}

} // namespace x2::input
