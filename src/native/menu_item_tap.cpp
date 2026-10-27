#include "menu_item_tap.hpp"

#include <algorithm>

namespace x2::native {
namespace {

/* PtInRect over the RECT onMouse builds. */
bool inside(const menu::SceneRect &rect, int x, int z) {
  return x >= rect.left && x < rect.right && z >= rect.top && z < rect.bottom;
}

bool shown(const menu::MenuItem &item) {
  return item.enabled() && !item.hidden();
}

const menu::MenuItem *at(const menu::MenuSnapshot &menu, int item) {
  if (item < 0 || static_cast<std::size_t>(item) >= menu.items.size()) {
    return nullptr;
  }
  return &menu.items[static_cast<std::size_t>(item)];
}

} // namespace

bool menu_tap_row(const menu::MenuSnapshot &menu, int item) {
  const menu::MenuItem *row = at(menu, item);
  return row != nullptr && row->item_class == menu::ItemClass::text &&
         !row->label.empty() && shown(*row) &&
         std::find(menu.rows.begin(), menu.rows.end(), item) != menu.rows.end();
}

MenuItemTap menu_item_tap(const menu::MenuSnapshot &menu, int item, int x,
                          int z, std::uint32_t message) {
  MenuItemTap tap;
  const menu::MenuItem *target = at(menu, item);
  if (target == nullptr || !target->use_command.empty() || !shown(*target) ||
      !inside(target->rect, x, z)) {
    return tap;
  }
  if (menu_tap_row(menu, item)) {
    tap.hit = true;
    tap.accept = message == kMouseButtonUp && !target->has_left_right();
  }
  return tap;
}

} // namespace x2::native
