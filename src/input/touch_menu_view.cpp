#include "touch_menu_view.hpp"

#include "touch_menu_parts.hpp"
#include "touch_menu_shop.hpp"
#include "touch_menu_team.hpp"

#include <array>
#include <cctype>

namespace x2::input {
namespace {

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

} // namespace

bool TouchMenuView::same_screen(const TouchMenuView &other) const {
  if (address != other.address || menu != other.menu ||
      rows.size() != other.rows.size()) {
    return false;
  }
  for (std::size_t i = 0; i < rows.size(); ++i) {
    if (rows[i].slot != other.rows[i].slot ||
        rows[i].entry != other.rows[i].entry) {
      return false;
    }
  }
  return true;
}

namespace {

/* The main menu, Options and the PDA: their up/down rows as the game orders
   them. */
std::optional<TouchMenuView>
build_row_menu_view(const menu::MenuSnapshot &menu,
                    const presentation::RetailScenePlane &plane) {
  TouchMenuView view = start_menu_view(menu);
  for (const int index : menu.rows) {
    const menu::MenuItem &item = menu.items[static_cast<std::size_t>(index)];
    if (!menu_item_shown(item)) {
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
    row.click = menu_item_centre(item.rect, plane);
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

using ViewBuilder = std::optional<TouchMenuView> (*)(
    const menu::MenuSnapshot &, const presentation::RetailScenePlane &);

struct ReplacedClass {
  std::string_view menu_class;
  ViewBuilder build;
};

constexpr std::array<ReplacedClass, 5> kReplacedClasses = {{
    {"CMenuMain", build_row_menu_view},
    {"CMenuOptions", build_row_menu_view},
    {"CMenuPDA", build_row_menu_view},
    {"CMenuTeam", build_team_view},
    {"CMenuShop", build_shop_view},
}};

} // namespace

std::optional<TouchMenuView>
build_touch_menu_view(const menu::MenuSnapshot &menu,
                      const presentation::RetailScenePlane &plane) {
  if (menu.popup_up) {
    return std::nullopt;
  }
  for (const ReplacedClass &replaced : kReplacedClasses) {
    if (replaced.menu_class == menu.menu_class) {
      return replaced.build(menu, plane);
    }
  }
  return std::nullopt;
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
