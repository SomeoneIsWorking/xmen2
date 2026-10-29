#include "menu_touch_gaps.hpp"

#include "../native/menu_action.hpp"
#include "../native/menu_item_tap.hpp"
#include "../native/options_touch.hpp"
#include "../native/text_item_touch.hpp"
#include "../native/x2_log.h"
#include "list_touch_layout.hpp"
#include "retail_text.hpp"

#include <algorithm>
#include <string_view>

namespace x2::input {
namespace {

constexpr std::string_view kTokenPrefix = "$MENU_";
constexpr std::string_view kFooterPrefix = "desctext";
/* The item's name and, when it shows text, that text. */
std::string named(const menu::MenuItem &item) {
  const std::string text = retail_shown_text(item.label);
  return text.empty() ? item.name : item.name + " \"" + text + "\"";
}

/* What a tap at the centre of `index` does, by the pointer rule. */
native::MenuItemTap tap_centre(const menu::MenuSnapshot &menu, int index) {
  const menu::SceneRect &rect =
      menu.items[static_cast<std::size_t>(index)].rect;
  return native::menu_item_tap(menu, index, (rect.left + rect.right) / 2,
                               (rect.top + rect.bottom) / 2,
                               native::kMouseButtonUp);
}

/* Whether the menu's own onMouse steps rows (CMenuOptions steps its bars). */
bool steppable(const menu::MenuSnapshot &menu) {
  return menu.on_mouse != menu::kMenuOnMouse;
}

bool shown(const menu::MenuItem &item) {
  return item.enabled() && !item.hidden();
}

bool is_list(menu::ItemClass item_class) {
  switch (item_class) {
  case menu::ItemClass::list:
  case menu::ItemClass::list_box:
  case menu::ItemClass::list_codex:
  case menu::ItemClass::list_items:
  case menu::ItemClass::list_cycle:
  case menu::ItemClass::list_chars:
  case menu::ItemClass::skills:
    return true;
  default:
    return false;
  }
}

std::string_view token_in(std::string_view label) {
  const std::size_t at = label.find(kTokenPrefix);
  if (at == std::string_view::npos) {
    return {};
  }
  std::size_t end = at;
  while (end < label.size() && label[end] != ' ' && label[end] != '\n') {
    ++end;
  }
  return label.substr(at, end - at);
}

float client_width(const menu::SceneRect &rect,
                   const presentation::RetailScenePlane &plane) {
  return static_cast<float>(rect.right - rect.left) *
         static_cast<float>(plane.client_width()) / plane.width();
}

float client_height(int scene_height,
                    const presentation::RetailScenePlane &plane) {
  return static_cast<float>(scene_height) *
         static_cast<float>(plane.client_height()) / plane.height();
}

bool small(const menu::SceneRect &rect,
           const presentation::RetailScenePlane &plane) {
  const float finger = menu_finger_target(plane);
  return client_width(rect, plane) < finger ||
         client_height(rect.bottom - rect.top, plane) < finger;
}

} // namespace

const char *menu_touch_gap_kind_name(MenuTouchGapKind kind) {
  switch (kind) {
  case MenuTouchGapKind::row_without_command:
    return "row-without-command";
  case MenuTouchGapKind::step:
    return "step";
  case MenuTouchGapKind::list:
    return "list";
  case MenuTouchGapKind::footer_token:
    return "footer-token";
  case MenuTouchGapKind::small_target:
    return "small-target";
  }
  return "unknown";
}

float menu_finger_target(const presentation::RetailScenePlane &plane) {
  return 0.1F * static_cast<float>(
                    std::min(plane.client_width(), plane.client_height()));
}

float menu_finger_units(const presentation::RetailScenePlane &plane) {
  return menu_finger_target(plane) * plane.height() /
         static_cast<float>(plane.client_height());
}

std::vector<MenuTouchGap>
find_menu_touch_gaps(const menu::MenuSnapshot &menu,
                     const presentation::RetailScenePlane &plane) {
  std::vector<MenuTouchGap> gaps;
  /* A popup takes the pointer first (popup+0x7c) and is native on touch. */
  if (menu.popup_up) {
    return gaps;
  }
  for (const int index : menu.rows) {
    const menu::MenuItem &item = menu.items[static_cast<std::size_t>(index)];
    if (!shown(item)) {
      continue;
    }
    if (list_has_touch_layout(item.item_class, menu.menu_class)) {
      continue;
    }
    if (is_list(item.item_class)) {
      gaps.push_back({MenuTouchGapKind::list, named(item)});
      if (item.list_box && client_height(item.list_box->row_height, plane) <
                               menu_finger_target(plane)) {
        gaps.push_back({MenuTouchGapKind::small_target, item.name});
      }
      continue;
    }
    if (item.has_left_right() && item.use_command.empty() && !steppable(menu)) {
      gaps.push_back({MenuTouchGapKind::step, named(item)});
    }
    if (item.use_command.empty()) {
      if (!item.has_left_right() && native::menu_tap_row(menu, index) &&
          !tap_centre(menu, index).accept) {
        gaps.push_back({MenuTouchGapKind::row_without_command, named(item)});
      }
    } else if (small(item.rect, plane) &&
               !native::text_has_finger_area(menu.menu_class, item.name) &&
               !native::options_touch_has_card(menu.menu_class, item.name)) {
      gaps.push_back({MenuTouchGapKind::small_target, item.name});
    }
  }
  for (const menu::MenuItem &item : menu.items) {
    if (!item.name.starts_with(kFooterPrefix) || !shown(item)) {
      continue;
    }
    const std::string_view token = token_in(item.label);
    if (token.empty()) {
      continue;
    }
    if (!native::menu_token_action(token) &&
        !native::touch_token_action(token) &&
        !native::menu_handles_token(menu.menu_class, token)) {
      gaps.push_back({MenuTouchGapKind::footer_token, std::string(token)});
    } else if (!native::text_has_touch_button(menu.menu_class, item.label) &&
               small(item.rect, plane)) {
      gaps.push_back({MenuTouchGapKind::small_target, item.name});
    }
  }
  return gaps;
}

void MenuTouchGapReport::observe(const menu::MenuSnapshot &menu,
                                 const presentation::RetailScenePlane &plane) {
  if (menu.address != open_menu_) {
    open_menu_ = menu.address;
    open_gaps_.clear();
  }
  for (MenuTouchGap &gap : find_menu_touch_gaps(menu, plane)) {
    Key key{menu.name, gap.kind, std::move(gap.item)};
    if (std::find(open_gaps_.begin(), open_gaps_.end(), key) !=
        open_gaps_.end()) {
      continue;
    }
    ++counts_[key];
    open_gaps_.push_back(std::move(key));
  }
}

void MenuTouchGapReport::report() const {
  for (const auto &[key, count] : counts_) {
    x2_log_info("menu touch gap: %s %s %s x%u\n", std::get<0>(key).c_str(),
                menu_touch_gap_kind_name(std::get<1>(key)),
                std::get<2>(key).c_str(), count);
  }
}

} // namespace x2::input
