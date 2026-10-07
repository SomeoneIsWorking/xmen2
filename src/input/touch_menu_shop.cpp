#include "touch_menu_shop.hpp"

#include "touch_menu_parts.hpp"

#include <array>
#include <string_view>

namespace x2::input {
namespace {

constexpr std::uint32_t kStashBit = 0x01u;
constexpr std::array<std::string_view, 3> kShopTabs = {
    "shop_option01", "shop_option02", "shop_option03"};
/* CMenuShop::onMouse (0x005d3400) tests these instead when menu+0x18e8 bit 0
   marks the stash. */
constexpr std::array<std::string_view, 3> kStashTabs = {
    "stash_option01", "stash_option02", "stash_option03"};
constexpr std::string_view kShopList = "list";
constexpr std::string_view kDescription = "item_desc";
constexpr std::string_view kCost = "item_cost_value";

/* A value item and the item naming it. With no label item, `name` names
   it: the money is an icon in the game. */
struct FactItems {
  std::string_view label;
  std::string_view value;
  std::string_view name;
};

constexpr std::array<FactItems, 4> kFacts = {{
    {"label_cost", kCost, ""},
    {"", "money_value", "money"},
    {"label_inventory_count", "inventory_count", ""},
    /* Its own text is label and value: "~02Limit:~~ 10". */
    {"", "label_owner", ""},
}};

/* CMenuShop's selection update (0x005d30d0) writes the cost in style ~06 when
   the money does not cover it. */
constexpr std::string_view kWarnStyle = "~06";

void append_facts(const menu::MenuSnapshot &menu, TouchMenuView *view) {
  for (const FactItems &fact : kFacts) {
    const menu::MenuItem *value = find_menu_item(menu, fact.value);
    if (value == nullptr || !menu_item_shown(*value)) {
      continue;
    }
    TouchMenuFact out;
    out.value = touch_menu_text(value->label);
    if (out.value.empty()) {
      continue;
    }
    out.warn = value->label.starts_with(kWarnStyle);
    if (fact.label.empty()) {
      out.label = std::string(fact.name);
    } else if (const menu::MenuItem *label = find_menu_item(menu, fact.label)) {
      out.label = touch_menu_text(label->label);
    }
    view->facts.push_back(std::move(out));
  }
  const menu::MenuItem *description = find_menu_item(menu, kDescription);
  if (description != nullptr && menu_item_shown(*description)) {
    view->detail = menu_text_lines(description->label);
  }
}

} // namespace

std::optional<TouchMenuView>
build_shop_view(const menu::MenuSnapshot &menu,
                const presentation::RetailScenePlane &plane) {
  if (!menu.mode) {
    return std::nullopt;
  }
  const bool stash = (*menu.mode & kStashBit) != 0u;
  const menu::MenuItem *list = find_menu_item(menu, kShopList);
  if (list == nullptr || !list->list_box || !menu_item_shown(*list)) {
    return std::nullopt;
  }
  TouchMenuView view = start_menu_view(menu);
  append_menu_tabs(menu, plane, stash ? kStashTabs : kShopTabs, &view);
  append_list_entries(*list, ListTap::select, &view);
  append_facts(menu, &view);
  /* The game prices only the selected entry. */
  const menu::MenuItem *cost = find_menu_item(menu, kCost);
  if (view.focused_row >= 0 && cost != nullptr && menu_item_shown(*cost)) {
    view.rows[static_cast<std::size_t>(view.focused_row)].value =
        touch_menu_text(cost->label);
  }
  append_menu_footers(menu, plane, &view);
  return view;
}

} // namespace x2::input
